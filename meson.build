project('apinette', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

curl = dependency('libcurl', version : '>=7')
lua = dependency('lua', version : '>=5.4') 
jansson = dependency('jansson', version : '>=2.13')
readline = dependency('readline', version : '>=8')

exe = executable('apinette',
  ['main.c', 'apinette.c', 'base64.c'],
  install : true,
  dependencies : [curl, lua, jansson, readline])

test('basic', exe)
