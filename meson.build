project('apinette', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

lua = dependency('lua')
curl = dependency('libcurl')
jansson = dependency('jansson')

executable('apinette',
           'main.c',
           'linenoise.c',
           'base64.c',
           'apinette.c',
           install : true,
           dependencies : [lua, curl, jansson])
