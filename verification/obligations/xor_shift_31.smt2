; Exact bitvector identity, unsigned logical shifts.
; benchmark generated from python API
(set-info :status unknown)
(declare-fun x () (_ BitVec 64))
(assert
 (let ((?x23 (bvxor x (bvlshr x (_ bv31 64)))))
(let ((?x17 (bvxor (bvxor ?x23 (bvlshr ?x23 (_ bv31 64))) (bvlshr ?x23 (_ bv62 64)))))
(and (distinct ?x17 x) true))))
(check-sat)
