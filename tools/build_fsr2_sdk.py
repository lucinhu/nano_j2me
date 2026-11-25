#!/usr/bin/env python3
"""Build AMD FSR2 2.2.1 for Vulkan on Linux. Requires Python 3.12+, a C++17
compiler, ar, glslangValidator and SPIRV-Tools. The prefix is self-contained
apart from libvulkan and the C++ runtime. Build products stay in --work.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request

FSR = ('GPUOpen-Effects/FidelityFX-FSR2', 'v2.2.1', '4d7261ef054b071de83c7d201f6850c274226bd3025f4a2274ef4968343bd534')
VK = ('KhronosGroup/Vulkan-Headers', 'v1.3.250', 'c4c5a706a1f8f4d329fec2909b8c3fef4a4be043f393dbde5ce1439daa1194ab')
OPTIONS = ['REPROJECT_USE_LANCZOS_TYPE', 'HDR_COLOR_INPUT', 'LOW_RESOLUTION_MOTION_VECTORS', 'JITTERED_MOTION_VECTORS', 'INVERTED_DEPTH', 'APPLY_SHARPENING']
BASE = ['FFX_GPU=1', 'FFX_GLSL=1', 'FFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0', 'FFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0', 'FFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1', 'FFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0', 'FFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2']


def unpack(spec, archive, work):
    repo, tag, digest = spec
    if not archive.exists():
        partial = archive.with_suffix('.download')
        urllib.request.urlretrieve(f'https://codeload.github.com/{repo}/tar.gz/refs/tags/{tag}', partial)
        with partial.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != digest:
                raise ValueError(f'{partial}: SHA-256 mismatch')
        partial.replace(archive)
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != digest:
            raise ValueError(f'{archive}: SHA-256 mismatch')
    with tarfile.open(archive) as tar:
        root = tar.getmembers()[0].name
        tar.extractall(work, filter='data')
    return work / root


def compile_variant(job):
    source, key, directory = job
    target = directory / f'{source.stem}-{key}.spv'
    flags = BASE + [f'FFX_FSR2_OPTION_{opt}={(key >> i) & 1}' for i, opt in enumerate(OPTIONS)] + [f'FFX_HALF={key >> 6}']
    cmd = ['glslangValidator', '-V', '--target-env', 'vulkan1.1', '-S', 'comp', '-Os', '-I'+str(source.parent), *['-D'+f for f in flags], '-o', str(target), str(source)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{source.name}, key={key}: {result.stdout}{result.stderr}')
    return target


def reflect(module):
    # Upstream names SRVs r_*, UAVs rw_*, constant buffers cb*, and binds
    # immutable samplers separately. Descriptor-set reflection excludes the latter.
    asm = subprocess.check_output(['spirv-dis', module], text=True)
    names = dict(re.findall(r'OpName (%\S+) "([^"]+)"', asm))
    groups = [[], [], []]
    for var, binding in re.findall(r'OpDecorate (%\S+) Binding (\d+)', asm):
        name = names[var]
        if name.startswith('s_'):
            continue
        index = next(i for i, prefix in enumerate(('rw_', 'r_', 'cb')) if name.startswith(prefix))
        groups[index].append((int(binding), name))
    return [sorted(g) for g in groups]


def header(source, modules, out):
    name = source.stem
    ids, seen, rows, lines = [], {}, [], []
    for module in modules:
        binary = module.read_bytes()
        if binary not in seen:
            idx = len(seen)
            seen[binary] = idx
            subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', module], check=True)
            groups = reflect(module)
            blob = f'{name}_{idx}'
            words = [int.from_bytes(binary[i:i+4], 'little') for i in range(0,len(binary),4)]
            lines.append(f'static const uint32_t {blob}[]={{'+','.join(map(hex,words))+'};')
            fields = [f'(const uint8_t*){blob}',f'sizeof({blob})',*[str(len(g)) for g in groups]]
            for i, group in enumerate(groups):
                if not group:
                    fields += ['nullptr','nullptr']
                    continue
                ns, bs = f'{blob}_n{i}', f'{blob}_b{i}'
                lines += [f'static const char* const {ns}[]={{'+','.join(f'"{n}"' for _,n in group)+'};', f'static const uint32_t {bs}[]={{'+','.join(str(b) for b,_ in group)+'};']
                fields += [ns,bs]
            rows.append('{'+','.join(fields)+'}')
        ids.append(seen[binary])
    bits = ' '.join('uint32_t FFX_FSR2_OPTION_'+o+':1;' for o in OPTIONS)
    lines += [f'union {name}_PermutationKey {{ uint32_t index; struct {{ {bits} uint32_t FFX_HALF:1; }}; }};', f'static const int g_{name}_IndirectionTable[]={{'+','.join(map(str,ids))+'};', f'static const ShaderInfo g_{name}_PermutationInfo[]={{'+',\n'.join(rows)+'};']
    (out / (name+'_permutations.h')).write_text('\n'.join(lines))
    print(f'{name}: {len(modules)} permutations, {len(rows)} validated modules',flush=True)


COMPAT = '''#include <cstdint>
#include <cwchar>
#include <climits>
#include <cstdlib>
template<size_t N> static inline void copyBindingName(wchar_t (&dest)[N], const char *ascii) {
 size_t i = 0;
 for (; i + 1 < N && ascii[i]; ++i) dest[i] = static_cast<unsigned char>(ascii[i]);
 dest[i] = 0;
}
#include <cstring>
#define _countof(a) (sizeof(a)/sizeof((a)[0]))
template<size_t N> static inline void wcscpy_s(wchar_t (&d)[N], const wchar_t *s) { std::wcsncpy(d,s,N-1); d[N-1]=0; }
struct ShaderInfo {
 const uint8_t *blobData; uint32_t blobSize,numStorageImageResources,numSampledImageResources,numUniformBufferResources;
 const char *const *storageImageResourceNames; const uint32_t *storageImageResourceBindings;
 const char *const *sampledImageResourceNames; const uint32_t *sampledImageResourceBindings;
 const char *const *uniformBufferResourceNames; const uint32_t *uniformBufferResourceBindings;
};
'''


def isolate_context_constants(src):
    # Keep dispatch scratch independent for concurrent emulator instances.
    private = src/'ffx_fsr2_private.h'
    field = '    Fsr2Constants               constants;'
    text = private.read_text()
    if text.count(field) != 1:
        raise ValueError('Unexpected FSR2 private context layout')
    private.write_text(text.replace(field, field+'\n    FfxConstantBuffer constantBuffers[4];'))

    core = src/'ffx_fsr2.cpp'
    text = core.read_text()
    decl = 'FfxConstantBuffer globalFsr2ConstantBuffers[4] = {\n'
    start = text.index(decl)
    end = text.index('\n};', start) + len('\n};')
    types = re.findall(r'\{ sizeof\((\w+)\) / sizeof\(uint32_t\) \}', text[start:end])
    if types != ['Fsr2Constants', 'Fsr2SpdConstants', 'Fsr2RcasConstants', 'Fsr2GenerateReactiveConstants']:
        raise ValueError('Unexpected FSR2 constant buffer layout')
    text = text[:start] + text[end:]
    zero = '    memset(context, 0, sizeof(FfxFsr2Context_Private));'
    if text.count(zero) != 1:
        raise ValueError('Unexpected FSR2 context initializer')
    sizes = [f'    context->constantBuffers[{i}].uint32Size = sizeof({t}) / sizeof(uint32_t);' for i, t in enumerate(types)]
    text = text.replace(zero, '\n'.join([zero, *sizes]))
    reactive = text.rindex('static FfxErrorCode generateReactiveMaskInternal(')
    old = 'globalFsr2ConstantBuffers'
    if text[:reactive].count(old) != 9 or text[reactive:].count(old) != 1:
        raise ValueError('Unexpected constant buffer use in FSR2 core')
    core.write_text(text[:reactive].replace(old, 'context->constantBuffers') +
                    text[reactive:].replace(old, 'contextPrivate->constantBuffers'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=Path('.local-tools/debug-runs/fsr2-sdk'))
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--fsr-archive',type=Path)
    parser.add_argument('--vulkan-archive',type=Path)
    parser.add_argument('-j',type=int,default=min(8,os.cpu_count() or 1))
    a = parser.parse_args()
    work = a.work.resolve()
    prefix = (a.prefix or work/'install').resolve()
    out, inc, lib = work/'generated', prefix/'include', prefix/'lib'
    for d in [out, inc, lib]: d.mkdir(parents=True,exist_ok=True)
    root = unpack(FSR,a.fsr_archive or work/'fsr.tgz',work)
    vulkan = unpack(VK,a.vulkan_archive or work/'vulkan.tgz',work)
    src = root/'src/ffx-fsr2-api'
    isolate_context_constants(src)
    # FSR2 2.2.1 unconditionally excludes host-visible device-local heaps.
    # Prefer dedicated VRAM when present, but retain UMA as a fallback.
    backend = src/'vk/ffx_fsr2_vk.cpp'
    text = backend.read_text()
    changes = [
        ('(1 << i)) && (memProperties.memoryTypes[i].propertyFlags & requestedProperties))',
         '(1u << i)) && (memProperties.memoryTypes[i].propertyFlags & requestedProperties) == requestedProperties)'),
        ('VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))\n                continue;',
         'VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {\n'
         '                if (bestCandidate == UINT32_MAX) {\n'
         '                    bestCandidate = i;\n'
         '                    outProperties = memProperties.memoryTypes[i].propertyFlags;\n'
         '                }\n                continue;\n            }'),
    ]
    for old, new in changes:
        if text.count(old) != 1:
            raise ValueError('Unexpected FSR2 2.2.1 Vulkan memory selector')
        text = text.replace(old, new)
    converter = '    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;'
    if text.count(converter) != 1:
        raise ValueError('Unexpected Vulkan reflection name conversion')
    text = text.replace(converter, '')
    text, count = re.subn(r'wcscpy_s\((outPipeline->\w+\[\w+\]\.name), converter\.from_bytes\((shaderBlob\.\w+\[\w+\])\)\.c_str\(\)\);',
                          r'copyBindingName(\1, \2);', text)
    if count != 3:
        raise ValueError('Unexpected Vulkan reflection name copies')
    blob = src/'vk/shaders/ffx_fsr2_shaders_vk.h'
    decl, count = re.subn(r'const char\*\*([ ]+bound\w+Names;)', r'const char* const*\1', blob.read_text())
    if count != 3:
        raise ValueError('Unexpected Vulkan reflection name tables')
    blob.write_text(decl)
    backend.write_text(text)
    # Storage image qualifiers must match the VkImageView formats used by
    # CreateResourceVK, including the floating-point component encoding.
    callbacks = src/'shaders/ffx_fsr2_callbacks_glsl.h'
    text = callbacks.read_text()
    for old, new in [('FSR2_BIND_UAV_DILATED_DEPTH, r16f)', 'FSR2_BIND_UAV_DILATED_DEPTH, r32f)'),
                     ('FSR2_BIND_UAV_PREPARED_INPUT_COLOR, rgba16)', 'FSR2_BIND_UAV_PREPARED_INPUT_COLOR, rgba16f)')]:
        if text.count(old) != 1:
            raise ValueError(f'Unexpected FSR2 2.2.1 GLSL declaration for {old}')
        text = text.replace(old, new)
    callbacks.write_text(text)
    for dirname in ['vulkan','vk_video']:
        shutil.copytree(vulkan/'include'/dirname,inc/dirname,dirs_exist_ok=True)
    for h in [*src.glob('*.h'),src/'vk/ffx_fsr2_vk.h',*src.glob('shaders/*.h')]:
        dest = inc/h.relative_to(src)
        dest.parent.mkdir(exist_ok=True)
        shutil.copy2(h,dest)
    (out/'compat.hpp').write_text(COMPAT)
    files = [src/'ffx_assert.cpp',src/'ffx_fsr2.cpp',src/'vk/ffx_fsr2_vk.cpp',src/'vk/shaders/ffx_fsr2_shaders_vk.cpp']
    def compile_cpp(f):
        obj = out/(f.stem+'.o')
        subprocess.run([os.getenv('CXX','c++'),'-std=c++17','-O2','-fPIC','-DNDEBUG','-DFFX_GCC','-fno-exceptions','-include',out/'compat.hpp','-I'+str(inc),'-I'+str(out),'-c',f,'-o',obj],check=True)
        return obj
    objects = [compile_cpp(f) for f in files[:-1]]
    with ThreadPoolExecutor(max_workers=a.j) as pool:
        for source in sorted((src/'shaders').glob('*_pass.glsl')):
            count = 64 if 'luminance_pyramid' in source.name else 128
            modules = pool.map(compile_variant, ((source,k,out) for k in range(count)))
            header(source, list(modules),out)
    objects.append(compile_cpp(files[-1]))
    archive = out/'libffx_fsr2_vk.a'
    subprocess.run([os.getenv('AR','ar'),'rcs',archive,*objects],check=True)
    shutil.copy2(archive,lib/archive.name)
    for pkg, folder in [('fsr2', root), ('vulkan-headers', vulkan)]:
        dest = prefix/'share/licenses'/pkg
        dest.mkdir(parents=True, exist_ok=True)
        for path in folder.glob('LICENSE*'):
            if path.is_file(): shutil.copy2(path,dest/path.name)
    (prefix/'README.md').write_text(
        '# AMD FidelityFX FSR2 Vulkan SDK\n\n'
        'This build uses AMD FSR2 v2.2.1 and Khronos Vulkan-Headers v1.3.250. '
        'Include `ffx_fsr2.h` and `vk/ffx_fsr2_vk.h` with `FFX_GCC` defined. '
        'Link `lib/libffx_fsr2_vk.a`, the Vulkan loader, and the C++ runtime.\n\n'
        'Each context owns its four dispatch constant buffers (1040 bytes in total). '
        'Context creation initializes their sizes before scheduling compute work. '
        'The Vulkan memory selector supports unified memory, and the dilated depth '
        'and prepared input color shaders declare the formats of their bound images.\n')
    print(prefix)

if __name__ == '__main__':
    main()
