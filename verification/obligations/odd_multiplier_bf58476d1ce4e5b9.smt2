; CAELUM exact-arithmetic obligation: odd_multiplier_bf58476d1ce4e5b9
(set-option :timeout 30000)
(set-logic QF_BV)
(declare-const x (_ BitVec 64))
(assert (not (= (bvmul x (bvmul (_ bv13787848793156543929 64) (_ bv10871156337175269513 64))) x)))
(check-sat)
