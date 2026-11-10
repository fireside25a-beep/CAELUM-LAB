; CAELUM exact-arithmetic obligation: odd_multiplier_94d049bb133111eb
(set-option :timeout 30000)
(set-logic QF_BV)
(declare-const x (_ BitVec 64))
(assert (not (= (bvmul x (bvmul (_ bv10723151780598845931 64) (_ bv3573116690164977347 64))) x)))
(check-sat)
