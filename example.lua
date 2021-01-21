abra = api {
  proto = http,
  host = 'localhost:8081',
  path = '/data',
  user = 'Supervisor',
  passwd = ''
}

abra.parallel {
  abra.get '/issuedinvoices',
  abra.get '/firms',
  abra.get '/issuedorder',
  abra.post '/firms' 
}
