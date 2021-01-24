abra = api {
  proto = http,
  host = 'localhost:8081',
  path = '/data',
  auth = basic { user = 'Supervisor', password = '' },
  verbose = false
}
