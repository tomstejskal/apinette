project('apinette', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

curl = dependency('libcurl')
lua = dependency('lua')
jansson = dependency('jansson')
readline = dependency('readline')

exe = executable('apinette',
  ['main.c', 'apinette.c', 'base64.c'],
  install : true,
  dependencies : [curl, lua, jansson, readline])

test('basic', exe)
