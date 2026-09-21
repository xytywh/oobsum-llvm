(*
 * MiniOobChecker: entry point, registered in RegisterCheckers.
 *)

open! IStd

val checker : MiniOobDomain.summary InterproceduralAnalysis.t -> MiniOobDomain.summary option
