google = endpoint {
  proto = "https",
  host = 'www.google.com',
}

result = send(google.get "?q=lua")

if result.err then
  error(result.err)
elseif result.status >= 400 then
  error(string.format("request failed with status: %d", result.status))
else
  print(string.format("request succeeded (%d, %g ms)", result.status, result.total_time * 1000.0))
end
