; Exact bitvector identity, unsigned logical shifts.
; benchmark generated from python API
(set-info :status unknown)
(declare-fun x () (_ BitVec 64))
(assert
 (let ((?x8 (bvxor x (bvlshr x (_ bv30 64)))))
(let ((?x13 (bvxor (bvxor ?x8 (bvlshr ?x8 (_ bv30 64))) (bvlshr ?x8 (_ bv60 64)))))
(and (distinct ?x13 x) true))))
(check-sat)
