#ifndef RECOVERY_PAGE
#define RECOVERY_PAGE

const char recovery_page[] = "<html>"
"<style>"
"body {"
"    background-color: #000;"
"    color: #00ff00;"
"    font-family: courier new;"
"}"
"</style>"
"<head>"
"<title>AxeOS Recovery</title>"
"</head>"
"<body>"
"<pre>"
"                      ____   _____\n"
"     /\\              / __ \\ / ____|\n"
"    /  \\   __  _____| |  | | (___\n"
"   / /\\ \\  \\ \\/ / _ \\ |  | |\\___ \\\n"
"  / ____ \\  >  <  __/ |__| |____) |\n"
" /_/___ \\_\\/_/\\_\\___|\\____/|_____/\n"
" |  __ \\\n"
" | |__) |___  ___ _____   _____ _ __ _   _\n"
" |  _  // _ \\/ __/ _ \\ \\ / / _ \\ '__| | | |\n"
" | | \\ \\  __/ (_| (_) \\ V /  __/ |  | |_| |\n"
" |_|  \\_\\___|\\___\\___/ \\_/ \\___|_|   \\__, |\n"
"                                      __/ |\n"
"                                     |___/\n"
"</pre>"
"<p>Please upload www.bin to recover AxeOS</p>"
"<p>After clicking upload, please wait 60 seconds<br>"
"DO NOT restart the device until response is received</p>"
"<input type=\"file\" id=\"wwwfile\" name=\"wwwfile\"><br>"
"<button id=\"upload\">Upload</button>"
"<small id=\"status\"></small>"
"<br><button id=\"restart\">Restart</button>"
"<br><br>Response:<br>"
"<small id=\"response\"></small>"
"<script>"
"document.getElementById('upload').addEventListener('click', handleUpload);"
"statusMsg = document.getElementById('status');"
"responseMsg = document.getElementById('response');"
"function handleUpload() {"
"  const fileInput = document.getElementById('wwwfile');"
"  const file = fileInput.files[0];"
"  const uploadUrl = '/api/system/OTAWWW';"
"  const upload_xhr = new XMLHttpRequest();"
"  upload_xhr.open('POST', uploadUrl, true);"
"  upload_xhr.setRequestHeader('Content-Type', 'application/octet-stream');"
"  upload_xhr.onload = function() {"
"      const responseBody = upload_xhr.responseText;"
"      if (upload_xhr.status === 200) {"
"          statusMsg.innerHTML = 'Upload successful!';"
"      } else {"
"          statusMsg.innerHTML='Error uploading!';"
"      }"
"      responseMsg.innerHTML = responseBody;"
"  };"
"  statusMsg.innerHTML = 'uploading...';"
"  upload_xhr.send(file);"
"}"
"document.getElementById('restart').addEventListener('click', handleRestart);"
"function handleRestart() {"
"  const restartUrl = '/api/system/restart';"
"  const restart_xhr = new XMLHttpRequest();"
"  restart_xhr.open('POST', restartUrl, true);"
"  restart_xhr.setRequestHeader('Content-Type', '');"
"  restart_xhr.onload = function() {"
"    const responseBody = restart_xhr.responseText;"
"    responseMsg.innerHTML = responseBody;"
"  };"
"  restart_xhr.send(null);"
"}"
"</script>"
"</body>"
"</html>";

#endif
