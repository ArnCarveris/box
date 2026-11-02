from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
source = (root / 'src/geometry/surface_math.c').read_text()
source = source[source.index('static box_sv sv('):source.index('void box_surface_sample(')]
source = re.sub(r'static (box_sv|float) (\w+)\((.*?)\)\s*\{',
    lambda m: 'fn ' + m[2] + '(' + ', '.join(
        part.strip().split()[-1].lstrip('*') + ': ' + ('SurfaceData' if '*' in part else
        {'box_sv': 'vec3f', 'float': 'f32', 'int': 'i32'}[part.strip().split()[0]])
        for part in m[3].split(',')) + ') -> ' +
    {'box_sv': 'vec3f', 'float': 'f32'}[m[1]] + ' {', source, flags=re.S)
source = source.replace('return (box_sv){x, y, z};', 'return vec3f(x, y, z);')
source = source.replace('return d->p[i][j];', 'return d.p[i][j];')
source = source.replace('return d->assets[i - 24][j];', 'return surface_assets[i - 24][j];')
source = re.sub(r'\b(box_sv|float|int) (\w+) =', r'var \2 =', source)
source = re.sub(r'\(float\)\(([^()]*)\)', r'f32(\1)', source)
source = re.sub(r'\(float\)(\w+)', r'f32(\1)', source)
source = re.sub(r'\(int\)(floorf\([^;]*\)|sf\([^)]*\)|fminf\([^;]*\))', r'i32(\1)', source)
source = re.sub(r'(?<![\w])(\d+(?:\.\d*)?)f\b', r'\1', source)
source = re.sub(r'\b(sqrtf|fminf|fmaxf|fabsf|floorf|cosf|sinf)\b',
    lambda m: {'sqrtf':'sqrt', 'fminf':'min', 'fmaxf':'max', 'fabsf':'abs',
        'floorf':'floor', 'cosf':'cos', 'sinf':'sin'}[m[0]], source)
source = source.replace('i ++', 'i ++')
output = '#pragma once\n\n#include "curve.wgsl"\n\nstruct SurfaceData {\n    p: array<vec4f, 24>,\n};\n\n' + source
(root / 'etc/shaders/include/surface.wgsl').write_text(output)
