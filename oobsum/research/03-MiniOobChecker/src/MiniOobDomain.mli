(*
 * MiniOobChecker: a minimal, summary-based out-of-bounds checker.
 *
 * This is a teaching-sized re-implementation of the two ideas that make Infer's
 * BufferOverrun work across procedure boundaries:
 *   1. an abstract value that can name a *formal parameter* symbolically, so that
 *      facts survive outside the procedure they were discovered in;
 *   2. a set of *safety conditions* ("obligations") carried in the summary, which
 *      the caller instantiates by substituting its own actuals for the callee's
 *      formals -- see BufferOverrunProofObligations.ConditionSet.{subst,for_summary}.
 *)

open! IStd

(** A symbolic integer: either a literal, or "the value of formal #k, plus a constant". That second
    form is what lets an index discovered inside a callee be re-expressed at a call site. *)
module Idx : sig
  type t = Const of int | Formal of int * int | Top [@@deriving compare, equal]

  val pp : Format.formatter -> t -> unit

  val plus_const : t -> int -> t
end

(** What an array access is based on: a local array whose element count we know, or the pointer that
    arrived as formal #k (whose extent is unknown here and must be resolved by a caller). *)
module Base : sig
  type t = Local of {name: string; size: int} | Formal of int [@@deriving compare, equal]

  val pp : Format.formatter -> t -> unit
end

(** One safety condition: "[idx] must be within the bounds of [base]", recorded at [loc]. *)
module Obligation : sig
  type t = {base: Base.t; idx: Idx.t; loc: Location.t} [@@deriving compare, equal]

  val pp : Format.formatter -> t -> unit
end

(** The abstract value of a program variable or temporary. *)
module Val : sig
  type t = {idx: Idx.t; base: Base.t option} [@@deriving compare, equal]

  val top : t

  val pp : Format.formatter -> t -> unit
end

include AbstractDomain.S

val initial : formals:(Var.t * int) list -> arrays:(Var.t * string * int) list -> t
(** Entry state: each formal #k is bound to the symbolic value [Formal (k, 0)] and, if it is a
    pointer, to the symbolic base [Base.Formal k]; each local array is bound to a [Base.Local] with
    its element count. *)

val find : Var.t -> t -> Val.t

val set : Var.t -> Val.t -> t -> t

val add_obligation : Obligation.t -> t -> t

val obligations : t -> Obligation.t list

type summary = {conditions: Obligation.t list} [@@deriving compare, equal]

val pp_summary : Format.formatter -> summary -> unit

val summary_of_post : t -> summary
(** Keep only the conditions that are still open, i.e. those phrased in terms of formals. Conditions
    about the procedure's own locals were already decided here and never escape. *)
