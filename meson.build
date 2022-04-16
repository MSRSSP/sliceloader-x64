project('sliceloader', 'cpp',
        default_options : ['cpp_std=c++17'])

realmode_elf = executable(
  'realmode.elf',
  files('realmode.S'),
  link_args: ['-nostdlib', '-no-pie', '-Wl,--build-id=none', '-Wl,-T' + meson.current_source_dir() / 'realmode.lds'],
)

objcopy = find_program('objcopy', required: true)

realmode_bin = custom_target(
  'realmode.bin',
  output: 'realmode.bin',
  input: realmode_elf,
  command: [objcopy, '-O', 'binary', '--strip-debug', '@INPUT@', '@OUTPUT@'],
)

# Workaround Meson bug. This is the "dirty, filthy, terrible hack" suggested at
# https://github.com/mesonbuild/meson/issues/10241#issuecomment-1087964333
realmode_bin_kludge = custom_target(
  output: 'realmode_bin_kludge.h',
  input: realmode_bin,
  command: ['touch', '@OUTPUT@'],
)

iasl = find_program('iasl', required: true) # apt install acpica-tools

dsdt_aml = custom_target(
  output: 'dsdt.aml',
  input: files('dsdt.asl'),
  command: [iasl, '-w3', '-we', '-p', '@OUTDIR@/@BASENAME@', '@INPUT@'],
  build_by_default: true,
)

executable(
  'runslice',
  files(
    'acpi.cpp',
    'lapic.cpp',
    'loader.cpp',
    'lowmem.cpp',
    'realmode_blob.S',
    'runslice.cpp',
  ) + [realmode_bin_kludge],
  cpp_args: ['-DREALMODE_BIN_PATH="' + realmode_bin.full_path() + '"'],
  link_args: ['-z', 'noexecstack'],
)
