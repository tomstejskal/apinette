project('apinette', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

curl = dependency('libcurl', version : '>=7')
lua = dependency('lua', version : '>=5.4') 
cjson = dependency('libcjson', version : '>=1.7')
readline = dependency('readline', version : '>=8')

exe = executable('apinette',
  ['main.c', 'apinette.c', 'base64.c'],
  install : true,
  dependencies : [curl, lua, cjson, readline])

test('basic', exe)
