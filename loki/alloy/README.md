

# Alloy NGINX configuration

NGINX supports writing logs in JSON format, which is a format which works perfectly with Alloy.


See `nginx_config.alloy` for an example Alloy configuration.



## NGINX log JSON format for Alloy


The following is an example for a log configuration which can be added to a NGINX configuration.
Alloy can parse those logs and send them to Loki.


```
  log_format loki_json escape=json
    '{'
      '"time":"$time_iso8601",'
      '"remote_addr":"$remote_addr",'
      '"request":"$request",'
      '"status":$status,'
      '"method":"$request_method",'
      '"uri":"$uri",'
      '"bytes_sent":$bytes_sent,'
      '"request_time":$request_time,'
      '"upstream_time":"$upstream_response_time"'
    '}';

    access_log /nginx-logs/access.json loki_json;
```

