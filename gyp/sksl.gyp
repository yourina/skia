# Copyright 2016 Google Inc.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'sksl',
      'type': 'static_library',
      'standalone_static_library': 1,
      'msvs_disabled_warnings': [ 4267 ],
      'sources': [ '<!@(python read_gni.py ../gn/sksl.gni skia_sksl_sources)' ],
      'include_dirs': [
         '../include/config',
         '../include/core',
         '../include/gpu',
         '../include/private',
         '../include/utils',
         '../src/gpu',
         '../src/sksl',
      ],
      'defines': [
        'SKIA'
      ],
      'all_dependent_settings': {
        'include_dirs': [
          '<(skia_src_path)/sksl',
        ],
      },
    },
  ],
}