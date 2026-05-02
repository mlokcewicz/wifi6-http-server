//------------------------------------------------------------------------------

/// @file index.c
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

const char *index_page = 
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html\r\n"
"Connection: close\r\n"
"\r\n"
    
"<style>"

"body {"
"    background-color: #121212;"
// "    background-color: #000000;"
"    color: #e0e0e0;"
"    font-family: system-ui, Arial, sans-serif;"
"    line-height: 1.6;"
"}"

".container {"
"    max-width: 980px;"     
"    margin: 40px auto;"    
"    padding: 0 16px;"      
// "    background-color: #000000;"  
// "    background-color: #121212;"
"    border-radius: 8px;"          
// "    border: 1px solid #e0e0e0;"
"}"

"a {"
"    color: #4da3ff;"
"}"

"table {"
"    border-collapse: collapse;"
"    margin-bottom: 24px;"
"}"

"td, th {"
"    border: 1px solid #444;"
"    padding: 5px;"
"}"

".center-block {"
"    text-align: center;"
"}"

".center-block iframe {"
"    display: block;"
"    margin: 16px auto;"
"}"

".github-profile-badge {"
"    display: flex !important;"
"    justify-content: center !important;"
"    margin: 16px 0;"
"}"

".github-profile-badge-wrapper {"
"    display: inline-flex !important;"
"    align-items: center !important;"
"    justify-content: center !important;"
"}"

".github-profile-badge-img-wrapper {"
"    display: flex !important;"
"    align-items: center !important;"
"    justify-content: center !important;"
"}"

"</style>"

"<div class=\"container\">"

"<h1>Simple nRF7002 Demo server</h1>"
"<hr />"
"<h3>SENSOR DATA:</h3>"
"<table style=\"border-color: white;\" border=\"2\">"
"<tbody>"
"<tr>"
"<td><strong>Param</strong></td>"
"<td><strong>Value</strong></td>"
"</tr>"
"<tr>"
"<td>Uptime [s]</td>"
"<td>%lu</td>"
"</tr>"
"</tbody>"
"</table>"

"<h4>FIRMWARE INFO:</h4>"
"<table style=\"border-color: white;\" border=\"2\">"
"<tbody>"
"<tr>"
"<td><pre>%s</pre></td>"
// "<td><strong><pre>%s</pre></strong></td>"
"</tr>"
"</tbody>"
"</table>"
"<hr />"

"<div class=\"center-block\">"

"<h4>Check out my GitHub:</h4>"

"<div class=\"github-profile-badge\" data-user=\"mlokcewicz\"></div>"
"<script src=\"https://cdn.jsdelivr.net/gh/Rapsssito/github-profile-badge@latest/src/widget.min.js\"></script>"

// "<h4><a title=\"GitHub account\" href=\"https://github.com/mlokcewicz\">https://github.com/mlokcewicz</a></h4>"
"<h4>Check out my band:</h4>"

"<iframe data-testid=\"embed-iframe\" style=\"border-radius:12px\" src=\"https://open.spotify.com/embed/artist/04Vyrl9r3Uu6tTXQ1hwDWn?utm_source=generator&theme=0\" width=\"560\" height=\"152\" frameBorder=\"0\" allowfullscreen=\"\" allow=\"autoplay; clipboard-write; encrypted-media; fullscreen; picture-in-picture\" loading=\"lazy\"></iframe>"
"<h4><iframe title=\"YouTube video player\" src=\"//www.youtube.com/embed/XOluSuL-kZ0\" width=\"560\" height=\"315\" frameborder=\"0\" allowfullscreen=\"allowfullscreen\"></iframe></h4>"

"<hr />"

"</div>"

"</div>"
;

//------------------------------------------------------------------------------
