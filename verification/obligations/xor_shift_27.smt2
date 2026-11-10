; Exact bitvector identity, unsigned logical shifts.
; benchmark generated from python API
(set-info :status unknown)
(declare-fun x () (_ BitVec 64))
(assert
 (let ((?x85 (bvxor x (bvlshr x (_ bv27 64)))))
(let ((?x90 (bvxor (bvxor ?x85 (bvlshr ?x85 (_ bv27 64))) (bvlshr ?x85 (_ bv54 64)))))
(and (distinct ?x90 x) true))))
(check-sat)
