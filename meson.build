project('apinette', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

curl = dependency('libcurl', version : '>=7 && <8')
lua = dependency('lua', version : '>=5.3 && <6') 

exe = executable('apinette',
  ['main.c', 'base64.c'],
  install : true,
  dependencies : [curl])

test('basic', exe)
