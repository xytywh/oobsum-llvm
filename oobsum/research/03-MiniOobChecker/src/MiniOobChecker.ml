(*
 * MiniOobChecker: a minimal out-of-bounds checker that works ACROSS procedures by
 * reading callee summaries, never by inlining callee bodies.
 *
 * The structure mirrors infer/src/labs/04_interprocedural (the official lab) and the
 * real BufferOverrun pipeline:
 *
 *   TransferFunctions.exec_instr
 *     |-- Load  {e = Lindex (base, idx)}  -> record a safety condition, decide it if we can
 *     |-- Call  (_, Const (Cfun callee), actuals, _, _)
 *            -> analyze_dependency callee          (* Ondemand: cache -> SQLite -> analyse once *)
 *            -> substitute actuals for the callee's formals in each condition of its summary
 *            -> decide it here, or re-propagate it in terms of OUR formals
 *   checker
 *     -> AbstractInterpreter.MakeRPO(...).compute_post
 *     -> MiniOobDomain.summary_of_post  (the payload stored in the specs table)
 *
 * Note what is absent: nothing in this file ever loads the callee's Procdesc or walks its
 * CFG. The only thing that crosses the procedure boundary is the summary value.
 *)

open! IStd
module F = Format
module L = Logging
module Dom = MiniOobDomain

let log fmt = F.kasprintf (fun s -> L.progress "[MiniOOB] %s@." s) fmt

(** Bounds check on a fully-resolved condition. [None] = cannot decide. *)
let decide ~(base : Dom.Base.t) ~(idx : Dom.Idx.t) =
  match (base, idx) with
  | Dom.Base.Local {size}, Dom.Idx.Const v ->
      Some (v >= 0 && v < size)
  | _ ->
      None


module TransferFunctions (CFG : ProcCfg.S) = struct
  module CFG = CFG
  module Domain = Dom

  type analysis_data = Dom.summary InterproceduralAnalysis.t

  let int_of_const c = match (c : Const.t) with Cint i -> IntLit.to_int i | _ -> None

  (** Abstract evaluation of a SIL expression. Anything we do not model becomes Top, which costs
      precision but never soundness of the *reporting* decision (we only report when the condition
      resolves to a concrete violation). *)
  let rec eval astate (e : Exp.t) : Dom.Val.t =
    match e with
    | Const c -> (
      match int_of_const c with
      | Some n ->
          {Dom.Val.idx= Dom.Idx.Const n; base= None}
      | None ->
          Dom.Val.top )
    | Var id ->
        Dom.find (Var.of_id id) astate
    | Lvar pvar ->
        Dom.find (Var.of_pvar pvar) astate
    | Cast (_, e) ->
        eval astate e
    | BinOp ((PlusA _ | PlusPI), e1, Const c) | BinOp ((PlusA _ | PlusPI), Const c, e1) -> (
      match int_of_const c with
      | Some n ->
          let v = eval astate e1 in
          {v with Dom.Val.idx= Dom.Idx.plus_const v.Dom.Val.idx n}
      | None ->
          Dom.Val.top )
    | BinOp ((MinusA _ | MinusPI), e1, Const c) -> (
      match int_of_const c with
      | Some n ->
          let v = eval astate e1 in
          {v with Dom.Val.idx= Dom.Idx.plus_const v.Dom.Val.idx (-n)}
      | None ->
          Dom.Val.top )
    | _ ->
        Dom.Val.top


  (** Record one array access, and report immediately if it is decidable here. *)
  let check_access {InterproceduralAnalysis.proc_desc; err_log} astate ~base_exp ~idx_exp ~loc =
    let base = (eval astate base_exp).Dom.Val.base in
    let idx = (eval astate idx_exp).Dom.Val.idx in
    match base with
    | None ->
        astate
    | Some base -> (
        let o = {Dom.Obligation.base; idx; loc} in
        match decide ~base ~idx with
        | Some true ->
            log "  safe access %a" Dom.Obligation.pp o ;
            astate
        | Some false ->
            log "  OUT OF BOUNDS %a" Dom.Obligation.pp o ;
            Reporting.log_issue proc_desc err_log ~loc MiniOob IssueType.mini_oob
              (F.asprintf "Out-of-bounds access: %a" Dom.Obligation.pp o) ;
            astate
        | None ->
            log "  open condition %a" Dom.Obligation.pp o ;
            Dom.add_obligation o astate )


  (** Instantiate one condition of a callee summary at this call site: replace the callee's formals
      by our actuals. This is the miniature of
      BufferOverrunProofObligations.ConditionSet.subst. *)
  let instantiate ~actual_vals ({Dom.Obligation.base; idx; loc} as callee_obl) =
    let nth k = List.nth actual_vals k in
    let base' =
      match base with
      | Dom.Base.Local _ ->
          None (* a condition about the callee's own local: already decided there *)
      | Dom.Base.Formal k ->
          Option.bind (nth k) ~f:(fun (v : Dom.Val.t) -> v.Dom.Val.base)
    in
    let idx' =
      match idx with
      | Dom.Idx.Const _ | Dom.Idx.Top ->
          idx
      | Dom.Idx.Formal (k, c) -> (
        match nth k with
        | Some (v : Dom.Val.t) ->
            Dom.Idx.plus_const v.Dom.Val.idx c
        | None ->
            Dom.Idx.Top )
    in
    Option.map base' ~f:(fun base' -> {callee_obl with Dom.Obligation.base= base'; idx= idx'})


  let exec_call ({InterproceduralAnalysis.proc_desc; err_log; analyze_dependency} as analysis_data)
      astate ~callee ~actuals ~loc =
    match analyze_dependency callee with
    | Error _ ->
        log "no summary for %a (undefined/native) -- nothing to do" Procname.pp callee ;
        astate
    | Ok {Dom.conditions= callee_obligations} ->
        log "Load Summary %a (%d open condition(s))" Procname.pp callee
          (List.length callee_obligations) ;
        let actual_vals = List.map actuals ~f:(fun (e, _typ) -> eval astate e) in
        log "Apply Summary %a" Procname.pp callee ;
        List.fold callee_obligations ~init:astate ~f:(fun astate callee_obl ->
            match instantiate ~actual_vals callee_obl with
            | None ->
                astate
            | Some ({Dom.Obligation.base; idx} as o) -> (
                (* keep the callee's source location: that is where the access really is *)
                match decide ~base ~idx with
                | Some true ->
                    log "  discharged (safe): %a" Dom.Obligation.pp o ;
                    astate
                | Some false ->
                    log "  discharged (VIOLATED): %a" Dom.Obligation.pp o ;
                    Reporting.log_issue proc_desc err_log ~loc MiniOob IssueType.mini_oob
                      (F.asprintf "Out-of-bounds access inside `%a`: %a" Procname.pp callee
                         Dom.Obligation.pp o ) ;
                    astate
                | None ->
                    log "  still open, propagating to our own summary: %a" Dom.Obligation.pp o ;
                    Dom.add_obligation o astate ) )
        |> fun astate -> ignore analysis_data ; astate


  let exec_instr astate analysis_data _node _idx (instr : Sil.instr) =
    match instr with
    (* n$0 = *&x  -- read a program variable *)
    | Load {id; e= Lvar pvar} ->
        Dom.set (Var.of_id id) (Dom.find (Var.of_pvar pvar) astate) astate
    (* n$2 = *n$0[n$1]  /  n$1 = *&a[n$0]  -- the array read we care about *)
    | Load {id; e= Lindex (base_exp, idx_exp); loc} ->
        let astate = check_access analysis_data astate ~base_exp ~idx_exp ~loc in
        Dom.set (Var.of_id id) Dom.Val.top astate
    | Load {id; e} ->
        Dom.set (Var.of_id id) (eval astate e) astate
    (* *&a[n$0] = rhs -- an array write is checked exactly like a read *)
    | Store {e1= Lindex (base_exp, idx_exp); loc} ->
        check_access analysis_data astate ~base_exp ~idx_exp ~loc
    | Store {e1= Lvar pvar; e2} ->
        Dom.set (Var.of_pvar pvar) (eval astate e2) astate
    | Call ((ret_id, _), Const (Cfun callee), actuals, loc, _) ->
        let astate = exec_call analysis_data astate ~callee ~actuals ~loc in
        Dom.set (Var.of_id ret_id) Dom.Val.top astate
    | Call ((ret_id, _), _, _, _, _) ->
        (* indirect call (function pointer): we cannot resolve the callee, so we learn nothing *)
        Dom.set (Var.of_id ret_id) Dom.Val.top astate
    | Store _ | Prune _ | Metadata _ ->
        astate


  let pp_session_name _node fmt = F.pp_print_string fmt "mini oob checker"
end

module CFG = ProcCfg.Normal
module Analyzer = AbstractInterpreter.MakeRPO (TransferFunctions (CFG))

(** Collect the entry state: formals get symbolic values, local arrays get their extent. *)
let initial_state proc_desc =
  let formals =
    Procdesc.get_pvar_formals proc_desc
    |> List.mapi ~f:(fun k (pvar, _typ) -> (Var.of_pvar pvar, k))
  in
  let proc_name = Procdesc.get_proc_name proc_desc in
  let arrays =
    Procdesc.get_locals proc_desc
    |> List.filter_map ~f:(fun {ProcAttributes.name; typ} ->
        match typ.Typ.desc with
        | Typ.Tarray {length= Some len} ->
            IntLit.to_int len
            |> Option.map ~f:(fun size ->
                (Var.of_pvar (Pvar.mk name proc_name), Mangled.to_string name, size) )
        | _ ->
            None )
  in
  Dom.initial ~formals ~arrays


let checker ({InterproceduralAnalysis.proc_desc} as analysis_data) =
  let proc_name = Procdesc.get_proc_name proc_desc in
  log "Analyze %a" Procname.pp proc_name ;
  let initial = initial_state proc_desc in
  match Analyzer.compute_post analysis_data ~initial proc_desc with
  | None ->
      log "no post for %a" Procname.pp proc_name ;
      None
  | Some post ->
      let summary = Dom.summary_of_post post in
      log "Generate Summary %a: %a" Procname.pp proc_name Dom.pp_summary summary ;
      Some summary
