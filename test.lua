abra = api {
  proto = http,
  host = 'localhost:8081',
  path = '/data',
  auth = basic { user = 'Supervisor', password = '' }
}

result = parallel {
  abra.get '/firms',
  abra.get '/issuedinvoices',
  abra.get '/receivedinvoices',
  abra.get '/storecards',
  abra.get '/storeprices'
}
print(result)

--for req, resp in result do 
--  if resp.err then
--    print(resp.err)
--  else
--    print(req.url, ' - status: ', resp.status)
--    if resp.status >= 400 then
--      print(resp.raw_body)
--    end
--  end
--end

