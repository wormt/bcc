-- bcc compiler.

solution 'bcc'
   configurations 'release'
   language 'C'
   buildoptions {
      '-Wall',
      '-Werror',
      '-Wno-error=switch',
      '-Wno-unused',
      '-std=c99',
      '-pedantic',
      '-Wstrict-aliasing',
      '-Wstrict-aliasing=2',
      '-Wmissing-field-initializers',
      '-D_BSD_SOURCE',
   }
   flags {
      'Symbols',
   }
   includedirs {
      'src/',
   }

   project 'src'
      location 'build/src'
      kind 'ConsoleApp'
      targetname 'bcc'
      files {
         'src/*.c',
      }
      links {
         'src_token',
         'src_parse',
         'src_semantic',
         'src_codegen',
         'src_cache',
         'src_cache_d',
         'src_cache_r',
         'src_cache_s',
      }

   project 'src_token'
      location 'build/src_token'
      targetdir 'build/src_token'
      kind 'StaticLib'
      files {
         'src/parse/token/*.c'
      }
      includedirs {
         'src/parse',
      }

   project 'src_parse'
      location 'build/src_parse'
      targetdir 'build/src_parse'
      kind 'StaticLib'
      files {
         'src/parse/*.c'
      }

   project 'src_semantic'
      location 'build/src_semantic'
      targetdir 'build/src_semantic'
      kind 'StaticLib'
      files {
         'src/semantic/*.c'
      }

   project 'src_codegen'
      location 'build/src_codegen'
      targetdir 'build/src_codegen'
      kind 'StaticLib'
      files {
         'src/codegen/*.c'
      }

   project 'src_cache'
      location 'build/src_cache'
      targetdir 'build/src_cache'
      kind 'StaticLib'
      files {
         'src/cache/*.c'
      }

   project 'src_cache_d'
      location 'build/src_cache_d'
      targetdir 'build/src_cache_d'
      kind 'StaticLib'
      files {
         'src/cache/dump/*.c'
      }

   project 'src_cache_r'
      location 'build/src_cache_r'
      targetdir 'build/src_cache_r'
      kind 'StaticLib'
      files {
         'src/cache/restore/*.c'
      }

   project 'src_cache_s'
      location 'build/src_cache_s'
      targetdir 'build/src_cache_s'
      kind 'StaticLib'
      files {
         'src/cache/serial/*.c'
      }