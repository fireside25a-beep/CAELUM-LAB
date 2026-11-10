#!/usr/bin/env python3
"""Reproduce known-truth *mathematical* fixtures, never physical observations.
A separate native C11 program learns from the emitted CSV datasets.
"""
from pathlib import Path
from math import sin,cos
from random import Random

base=Path(__file__).resolve().parent
r=Random(2048)
with (base/'known_quadratic.csv').open('w') as good,(base/'alternate_quadratic.csv').open('w') as other:
    for _ in range(240):
        x,z=r.uniform(-2,2),r.uniform(-2,2)
        y=1.25+2*x*x-.75*x*z
        good.write(f'{x:.17g},{z:.17g},{y:.17g}\n')
        other.write(f'{x:.17g},{z:.17g},{y+.2*x*x:.17g}\n')
with (base/'harmonic_trajectory.csv').open('w') as file:
    for i in range(1100):
        t=i*.01
        file.write(f'{sin(t):.17g},{cos(t):.17g},{t:.17g}\n')
r=Random(1773)
with (base/'randomized_trial.csv').open('w') as file:
    for _ in range(400):
        treatment=1 if r.random()<.5 else 0
        response=2+1.2*treatment+r.gauss(0,.5)
        file.write(f'{treatment},{response:.17g}\n')
print('Generated four explicit known-truth scientific fixtures; no physical measurements claimed.')
