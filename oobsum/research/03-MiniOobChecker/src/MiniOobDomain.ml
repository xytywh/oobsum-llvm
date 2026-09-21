(*
 * MiniOobChecker domain -- see MiniOobDomain.mli for the design notes.
 *)

open! IStd
module F = Format

module Idx = struct
  type t = Const of int | Formal of int * int | Top [@@deriving compare, equal]

  let pp fmt = function
    | Const n ->
        F.fprintf fmt "%d" n
    | Formal (k, 0) ->
        F.fprintf fmt "arg%d" k
    | Formal (k, c) when c > 0 ->
        F.fprintf fmt "arg%d+%d" k c
    | Formal (k, c) ->
        F.fprintf fmt "arg%d-%d" k (-c)
    | Top ->
        F.pp_print_string fmt "T"


  let plus_const t c =
    match t with
    | Const n ->
        Const (n + c)
    | Formal (k, c0) ->
        Formal (k, c0 + c)
    | Top ->
        Top
end

module Base = struct
  type t = Local of {name: string; size: int} | Formal of int [@@deriving compare, equal]

  let pp fmt = function
    | Local {name; size} ->
        F.fprintf fmt "%s[0..%d)" name size
    | Formal k ->
        F.fprintf fmt "arg%d" k
end

module Obligation = struct
  type t = {base: Base.t; idx: Idx.t; loc: Location.t} [@@deriving compare, equal]

  let pp fmt {base; idx; loc} =
    F.fprintf fmt "0 <= %a < extent(%a) at %a" Idx.pp idx Base.pp base Location.pp loc
end

module Val = struct
  type t = {idx: Idx.t; base: Base.t option} [@@deriving compare, equal]

  let top = {idx= Idx.Top; base= None}

  let join x y = if equal x y then x else top

  let pp fmt {idx; base} =
    match base with
    | None ->
        Idx.pp fmt idx
    | Some b ->
        F.fprintf fmt "%a@@%a" Idx.pp idx Base.pp b
end

module VarMap = PrettyPrintable.MakePPMap (Var)

type t = {vars: Val.t VarMap.t; obligations: Obligation.t list}

let dedup = List.dedup_and_sort ~compare:Obligation.compare

(* A state is smaller when it knows *more* about variables (a missing binding is Top) and has
   accumulated no extra obligations. *)
let leq ~lhs ~rhs =
  VarMap.for_all
    (fun v rv ->
      match VarMap.find_opt v lhs.vars with Some lv -> Val.equal lv rv | None -> false )
    rhs.vars
  && List.for_all rhs.obligations ~f:(fun o ->
      List.mem lhs.obligations o ~equal:Obligation.equal )


let join a b =
  { vars=
      VarMap.merge
        (fun _ x y ->
          match (x, y) with Some x, Some y -> Some (Val.join x y) | _ -> None (* = Top *) )
        a.vars b.vars
  ; obligations= dedup (a.obligations @ b.obligations) }


(* The domain has finite height on every program: [Val.t] collapses to Top after one disagreement
   and the obligation list only grows with syntactically distinct array accesses, of which a
   procedure has finitely many. So widening is just join -- no threshold needed. *)
let widen ~prev ~next ~num_iters:_ = join prev next

let pp fmt {vars; obligations} =
  F.fprintf fmt "@[<v>vars: %a@,conds: [%a]@]" (VarMap.pp ~pp_value:Val.pp) vars
    (F.pp_print_list ~pp_sep:(fun f () -> F.fprintf f ", ") Obligation.pp)
    obligations


let initial ~formals ~arrays =
  let vars =
    List.fold formals ~init:VarMap.empty ~f:(fun acc (var, k) ->
        VarMap.add var {Val.idx= Idx.Formal (k, 0); base= Some (Base.Formal k)} acc )
  in
  let vars =
    List.fold arrays ~init:vars ~f:(fun acc (var, name, size) ->
        VarMap.add var {Val.idx= Idx.Top; base= Some (Base.Local {name; size})} acc )
  in
  {vars; obligations= []}


let find v {vars} = Option.value (VarMap.find_opt v vars) ~default:Val.top

let set v value astate = {astate with vars= VarMap.add v value astate.vars}

let add_obligation o astate = {astate with obligations= dedup (o :: astate.obligations)}

let obligations {obligations} = obligations

type summary = {conditions: Obligation.t list} [@@deriving compare, equal]

let pp_summary fmt {conditions} =
  if List.is_empty conditions then F.pp_print_string fmt "(no open safety conditions)"
  else
    F.fprintf fmt "@[<v>%a@]"
      (F.pp_print_list ~pp_sep:(fun f () -> F.fprintf f ", ") Obligation.pp)
      conditions


let summary_of_post (astate : t) =
  let open_conditions =
    List.filter astate.obligations ~f:(fun {Obligation.base} ->
        match base with Base.Formal _ -> true | Base.Local _ -> false )
  in
  {conditions= dedup open_conditions}
