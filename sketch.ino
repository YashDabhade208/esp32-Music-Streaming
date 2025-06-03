#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <FS.h>

// Wi-Fi credentials
const char* ssid = "Airtel_ravi_5267";
const char* password = "Housemates";

// Static IP config
IPAddress local_IP(192, 168, 1, 6);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// DuckDNS credentials
const char* duckdns_token = "9530948e-6496-4ea3-8583-d3c602a3f2a9";
const char* duckdns_domain = "songs4everybody";

WebServer server(80);

// Logging system
String debugLogs = "";
const int MAX_LOG_SIZE = 10000; // Limit log size to prevent memory issues

void addLog(String message) {
  String timestamp = String(millis());
  String logEntry = "[" + timestamp + "] " + message + "\n";
  Serial.print(logEntry);
  
  debugLogs += logEntry;
  
  // Trim logs if they get too long
  if (debugLogs.length() > MAX_LOG_SIZE) {
    int cutPos = debugLogs.indexOf('\n', debugLogs.length() - MAX_LOG_SIZE + 1000);
    if (cutPos > 0) {
      debugLogs = debugLogs.substring(cutPos + 1);
    }
  }
}

String getContentType(String filename) {
  addLog("Getting content type for: " + filename);
  if (filename.endsWith(".mp3")) return "audio/mpeg";
  else if (filename.endsWith(".wav")) return "audio/wav";
  else if (filename.endsWith(".ogg")) return "audio/ogg";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else return "application/octet-stream";
}

void updateDuckDNS() {
  addLog("Starting DuckDNS update...");
  WiFiClient client;
  
  if (client.connect("www.duckdns.org", 80)) {
    String request = "GET /update?domains=" + String(duckdns_domain) + "&token=" + String(duckdns_token) + " HTTP/1.1\r\n";
    request += "Host: www.duckdns.org\r\n";
    request += "Connection: close\r\n\r\n";
    
    client.print(request);
    
    // Wait for response with watchdog feeding
    unsigned long timeout = millis() + 10000; // 10 second timeout
    while (client.available() == 0 && millis() < timeout) {
      delay(50);
      yield(); // Feed the watchdog
    }
    
    if (client.available()) {
      String response = client.readString();
      if (response.indexOf("OK") > 0) {
        addLog("DuckDNS Update successful");
      } else {
        addLog("DuckDNS Update failed: " + response.substring(0, 100));
      }
    } else {
      addLog("DuckDNS Update timeout");
    }
    client.stop();
  } else {
    addLog("DuckDNS connection failed");
  }
}

// Handle file upload (via POST /upload)
void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File fsUploadFile;
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    addLog("Upload Start: " + filename + " (Size will be determined)");
    
    if (SPIFFS.exists(filename)) {
      SPIFFS.remove(filename);
      addLog("Removed existing file: " + filename);
    }
    
    fsUploadFile = SPIFFS.open(filename, FILE_WRITE);
    if (!fsUploadFile) {
      addLog("ERROR: Failed to open file for writing: " + filename);
      server.send(500, "text/plain", "Failed to create file");
      return;
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) {
      size_t written = fsUploadFile.write(upload.buf, upload.currentSize);
      addLog("Upload chunk written: " + String(written) + "/" + String(upload.currentSize) + " bytes");
      if (written != upload.currentSize) {
        addLog("ERROR: Write size mismatch!");
      }
    } else {
      addLog("ERROR: File handle not available for writing");
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      addLog("Upload completed: " + upload.filename + " (" + String(upload.totalSize) + " bytes)");
    }
    
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    addLog("Upload aborted: " + upload.filename);
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    server.send(500, "text/plain", "Upload Aborted");
    return;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  addLog("ESP32 Music Player starting...");
  addLog("Free heap: " + String(ESP.getFreeHeap()));

  if (!WiFi.config(local_IP, gateway, subnet)) {
    addLog("ERROR: STA Failed to configure static IP");
  }

  addLog("Connecting to WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
    delay(500);
    wifiAttempts++;
    if (wifiAttempts % 10 == 0) {
      addLog("WiFi connection attempt: " + String(wifiAttempts));
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    addLog("✅ Connected to WiFi");
    addLog("IP Address: " + WiFi.localIP().toString());
    addLog("Gateway: " + WiFi.gatewayIP().toString());
  } else {
    addLog("❌ Failed to connect to WiFi");
  }

  if (!SPIFFS.begin(true)) {
    addLog("❌ SPIFFS Mount Failed");
    return;
  } else {
    addLog("✅ SPIFFS mounted successfully");
    addLog("SPIFFS total: " + String(SPIFFS.totalBytes()) + " bytes");
    addLog("SPIFFS used: " + String(SPIFFS.usedBytes()) + " bytes");
  }

  // Note: ESP32 WebServer doesn't have setTimeout method
  // Upload timeouts are handled differently in the client-side JavaScript

  // Logs endpoint for remote debugging
  server.on("/logs", HTTP_GET, []() {
    addLog("Logs requested from: " + server.client().remoteIP().toString());
    String html = "<!DOCTYPE html><html><head><title>ESP32 Logs</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:monospace;background:#000;color:#0f0;padding:20px;}";
    html += "pre{white-space:pre-wrap;word-wrap:break-word;}</style>";
    html += "<script>setTimeout(()=>location.reload(),10000);</script></head>";
    html += "<body><h2>ESP32 Debug Logs (Auto-refresh every 10s)</h2>";
    html += "<pre>" + debugLogs + "</pre></body></html>";
    server.send(200, "text/html", html);
  });

  // Clear logs endpoint
  server.on("/clearlogs", HTTP_GET, []() {
    debugLogs = "";
    addLog("Logs cleared by user");
    server.send(200, "text/plain", "Logs cleared");
  });

  // Serve file list as JSON with better error handling
  server.on("/list", HTTP_GET, []() {
    addLog("File list requested");
    String fileList = "[";
    File root = SPIFFS.open("/");
    if (!root) {
      addLog("ERROR: Failed to open SPIFFS root");
      server.send(500, "application/json", "{\"error\":\"Failed to open filesystem\"}");
      return;
    }
    
    File file = root.openNextFile();
    bool first = true;
    int fileCount = 0;
    
    while (file) {
      String fileName = String(file.name());
      if (fileName.endsWith(".mp3") || fileName.endsWith(".wav") || fileName.endsWith(".ogg")) {
        if (!first) fileList += ",";
        fileList += "\"" + fileName + "\"";
        first = false;
        fileCount++;
      }
      file = root.openNextFile();
    }
    fileList += "]";
    
    addLog("Found " + String(fileCount) + " audio files");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", fileList);
  });

  // Upload page and main page combined with fixes
  server.on("/", HTTP_GET, []() {
    addLog("Main page requested from: " + server.client().remoteIP().toString());
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>ESP32 Music Player</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          body {
            font-family: 'Segoe UI', sans-serif;
            margin: 0;
            padding: 20px;
            background: #121212;
            color: white;
          }
          h1 {
            text-align: center;
            color: #00e676;
          }
          #search {
            width: 100%;
            padding: 10px;
            margin-bottom: 10px;
            border: none;
            border-radius: 5px;
            font-size: 16px;
            box-sizing: border-box;
          }
          .file-item {
            background: #1f1f1f;
            margin: 5px 0;
            padding: 10px;
            border-radius: 8px;
            display: flex;
            justify-content: space-between;
            align-items: center;
          }
          .file-item:hover {
            background: #333;
          }
          .file-name {
            cursor: pointer;
            flex-grow: 1;
            margin-right: 10px;
          }
          button.add-btn, button.remove-btn {
            background: #00e676;
            border: none;
            border-radius: 5px;
            color: black;
            padding: 5px 10px;
            cursor: pointer;
            font-weight: bold;
            margin-left: 5px;
          }
          button.add-btn:hover {
            background: #00c853;
          }
          button.remove-btn {
            background: #e53935;
            color: white;
          }
          button.remove-btn:hover {
            background: #b71c1c;
          }
          audio {
            width: 100%;
            margin-top: 20px;
            outline: none;
          }
          .toast {
            position: fixed;
            bottom: 10px;
            left: 50%;
            transform: translateX(-50%);
            background: #e53935;
            color: white;
            padding: 10px 20px;
            border-radius: 5px;
            display: none;
            animation: fadein 0.5s, fadeout 0.5s 2.5s;
            z-index: 10000;
          }
          .toast.success {
            background: #00e676;
            color: black;
          }
          .loader {
            border: 6px solid #1f1f1f;
            border-top: 6px solid #00e676;
            border-radius: 50%;
            width: 40px;
            height: 40px;
            animation: spin 1s linear infinite;
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            display: none;
            z-index: 9999;
            background: rgba(0,0,0,0.8);
            padding: 20px;
          }
          @keyframes spin {
            0% { transform: translate(-50%, -50%) rotate(0deg); }
            100% { transform: translate(-50%, -50%) rotate(360deg); }
          }
          @keyframes fadein {
            from { bottom: 0; opacity: 0; }
            to { bottom: 10px; opacity: 1; }
          }
          @keyframes fadeout {
            from { opacity: 1; }
            to { opacity: 0; }
          }
          #playlist {
            margin-top: 20px;
            background: #222;
            border-radius: 8px;
            padding: 10px;
            max-height: 300px;
            overflow-y: auto;
          }
          #playlist h2 {
            margin-top: 0;
            color: #00e676;
          }
          .playlist-item {
            padding: 8px;
            border-bottom: 1px solid #444;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-radius: 4px;
            margin-bottom: 2px;
          }
          .playlist-item:hover {
            background: #333;
          }
          .playlist-item.active {
            background: #00e676;
            color: black;
            font-weight: bold;
          }
          .playlist-controls {
            display: flex;
            gap: 5px;
          }
          form#uploadForm {
            margin-bottom: 20px;
            background: #1f1f1f;
            padding: 15px;
            border-radius: 8px;
            display: flex;
            gap: 10px;
            align-items: center;
            flex-wrap: wrap;
          }
          form#uploadForm input[type="file"] {
            color: white;
            flex-grow: 1;
            min-width: 200px;
          }
          form#uploadForm button {
            background: #00e676;
            border: none;
            border-radius: 5px;
            padding: 10px 20px;
            cursor: pointer;
            font-weight: bold;
            color: black;
          }
          form#uploadForm button:hover {
            background: #00c853;
          }
          form#uploadForm button:disabled {
            background: #666;
            cursor: not-allowed;
          }
          .debug-link {
            position: fixed;
            top: 10px;
            right: 10px;
            background: #333;
            color: white;
            padding: 5px 10px;
            text-decoration: none;
            border-radius: 5px;
            font-size: 12px;
          }
          .debug-link:hover {
            background: #555;
          }
        </style>
      </head>
      <body>
        <a href="/logs" class="debug-link" target="_blank">🐛 Debug Logs</a>
        <h1>ESP32 Music Player</h1>

        <form id="uploadForm" method="POST" action="/upload" enctype="multipart/form-data">
          <input type="file" name="file" accept=".mp3,.wav,.ogg" required />
          <button type="submit" id="uploadBtn">Upload</button>
        </form>

        <input id="search" type="text" placeholder="Search songs..." oninput="filterFiles()" />
        <div id="file-list"></div>

        <audio id="player" controls></audio>

        <div id="playlist">
          <h2>Playlist (<span id="playlist-count">0</span>)</h2>
          <div id="playlist-items"></div>
        </div>

        <div class="toast" id="toast">Message</div>
        <div id="loader" class="loader"></div>

        <script>
          let files = [];
          let playlist = [];
          let currentIndex = -1;

          function showLoader() {
            console.log('Showing loader');
            document.getElementById("loader").style.display = "block";
          }
          
          function hideLoader() {
            console.log('Hiding loader');
            document.getElementById("loader").style.display = "none";
          }
          
          function showToast(msg, isSuccess = false) {
            console.log('Toast:', msg);
            const toast = document.getElementById("toast");
            toast.innerText = msg;
            toast.className = isSuccess ? "toast success" : "toast";
            toast.style.display = "block";
            setTimeout(() => { toast.style.display = "none"; }, 3000);
          }

          async function loadFiles() {
            console.log('Loading files...');
            showLoader();
            try {
              const controller = new AbortController();
              const timeoutId = setTimeout(() => controller.abort(), 10000); // 10 second timeout
              
              const res = await fetch('/list', {
                signal: controller.signal,
                cache: 'no-cache'
              });
              clearTimeout(timeoutId);
              
              if (!res.ok) {
                throw new Error(`HTTP ${res.status}: ${res.statusText}`);
              }
              
              files = await res.json();
              console.log('Loaded', files.length, 'files');
              renderFileList(files);
              showToast(`Loaded ${files.length} songs`, true);
            } catch (e) {
              console.error('Error loading files:', e);
              if (e.name === 'AbortError') {
                showToast("Request timed out - please try again");
              } else {
                showToast("Failed to load file list: " + e.message);
              }
            } finally {
              hideLoader();
            }
          }

          function renderFileList(fileArray) {
            const list = document.getElementById("file-list");
            list.innerHTML = "";
            if (fileArray.length === 0) {
              list.innerHTML = "<p style='text-align:center;color:#666;'>No songs found</p>";
              return;
            }
            fileArray.forEach(file => {
              const div = document.createElement("div");
              div.className = "file-item";

              const nameSpan = document.createElement("span");
              nameSpan.className = "file-name";
              nameSpan.innerText = file.substring(1); // remove leading '/'
              nameSpan.onclick = () => playFile(file);
              div.appendChild(nameSpan);

              const addBtn = document.createElement("button");
              addBtn.className = "add-btn";
              addBtn.innerText = "+ Add";
              addBtn.onclick = (e) => {
                e.stopPropagation();
                addToPlaylist(file);
              };
              div.appendChild(addBtn);

              list.appendChild(div);
            });
          }

          function filterFiles() {
            const query = document.getElementById("search").value.toLowerCase();
            const filtered = files.filter(f => f.toLowerCase().includes(query));
            renderFileList(filtered);
          }

          function addToPlaylist(file) {
            if (!playlist.includes(file)) {
              playlist.push(file);
              renderPlaylist();
              showToast("Added to playlist", true);
              if (currentIndex === -1) {
                currentIndex = 0;
                playFile(playlist[currentIndex]);
              }
            } else {
              showToast("Song already in playlist");
            }
          }

          function removeFromPlaylist(index) {
            console.log('Removing from playlist, index:', index, 'current:', currentIndex);
            const removedSong = playlist[index].substring(1);
            
            if (index === currentIndex) {
              // If removing currently playing song
              playlist.splice(index, 1);
              if (playlist.length === 0) {
                currentIndex = -1;
                document.getElementById("player").pause();
                document.getElementById("player").src = "";
              } else {
                currentIndex = index >= playlist.length ? 0 : index;
                playFile(playlist[currentIndex]);
              }
            } else if (index < currentIndex) {
              playlist.splice(index, 1);
              currentIndex--;
            } else {
              playlist.splice(index, 1);
            }
            renderPlaylist();
            showToast(`Removed "${removedSong}" from playlist`, true);
          }

          function renderPlaylist() {
            const playlistDiv = document.getElementById("playlist-items");
            const countSpan = document.getElementById("playlist-count");
            
            countSpan.innerText = playlist.length;
            playlistDiv.innerHTML = "";
            
            if (playlist.length === 0) {
              playlistDiv.innerHTML = "<p style='text-align:center;color:#666;'>Playlist is empty</p>";
              return;
            }
            
            playlist.forEach((file, index) => {
              const div = document.createElement("div");
              div.className = "playlist-item" + (index === currentIndex ? " active" : "");

              const nameSpan = document.createElement("span");
              nameSpan.innerText = `${index + 1}. ${file.substring(1)}`;
              nameSpan.style.flexGrow = "1";
              nameSpan.style.cursor = "pointer";
              nameSpan.onclick = () => {
                currentIndex = index;
                playFile(file);
                renderPlaylist();
              };
              div.appendChild(nameSpan);

              const controlsDiv = document.createElement("div");
              controlsDiv.className = "playlist-controls";

              const removeBtn = document.createElement("button");
              removeBtn.className = "remove-btn";
              removeBtn.innerText = "x";
              removeBtn.title = "Remove from playlist";
              removeBtn.onclick = (e) => {
                e.stopPropagation();
                removeFromPlaylist(index);
              };
              controlsDiv.appendChild(removeBtn);

              div.appendChild(controlsDiv);
              playlistDiv.appendChild(div);
            });
          }

          function playFile(file) {
            console.log('Playing file:', file);
            const player = document.getElementById("player");
            showLoader();
            
            player.src = file;
            player.load(); // Force reload
            
            player.onloadstart = () => {
              console.log('Load started');
            };
            
            player.oncanplay = () => {
              console.log('Can play');
              hideLoader();
              player.play().catch(e => {
                console.error('Play failed:', e);
                showToast('Failed to play: ' + e.message);
                hideLoader();
              });
            };
            
            player.onerror = (e) => {
              console.error('Player error:', e);
              hideLoader();
              showToast('Error loading audio file');
            };
            
            highlightCurrentPlaying(file);
          }

          function highlightCurrentPlaying(file) {
            const fileItems = document.querySelectorAll(".file-name");
            fileItems.forEach(item => {
              if (item.innerText === file.substring(1)) {
                item.style.color = "#00e676";
                item.style.fontWeight = "bold";
              } else {
                item.style.color = "white";
                item.style.fontWeight = "normal";
              }
            });
          }

          // Auto play next song when current ends
          document.getElementById("player").addEventListener("ended", () => {
            console.log('Song ended, playlist length:', playlist.length);
            if (playlist.length === 0) return;
            currentIndex++;
            if (currentIndex >= playlist.length) currentIndex = 0;
            playFile(playlist[currentIndex]);
            renderPlaylist();
          });

          // Upload form handling with improved timeout handling
          document.getElementById("uploadForm").addEventListener("submit", function(e) {
            const btn = document.getElementById("uploadBtn");
            const fileInput = this.querySelector('input[type="file"]');
            
            if (!fileInput.files[0]) {
              e.preventDefault();
              showToast("Please select a file");
              return;
            }
            
            btn.disabled = true;
            btn.innerText = "Uploading...";
            showLoader();
            
            // Create XMLHttpRequest for better upload handling
            e.preventDefault();
            const formData = new FormData();
            formData.append('file', fileInput.files[0]);
            
            const xhr = new XMLHttpRequest();
            xhr.timeout = 60000; // 60 second timeout
            
            xhr.onload = function() {
              btn.disabled = false;
              btn.innerText = "Upload";
              hideLoader();
              
              if (xhr.status === 200 || xhr.status === 303) {
                showToast("Upload successful!", true);
                fileInput.value = ""; // Clear file input
                setTimeout(loadFiles, 1000); // Reload file list
              } else {
                showToast("Upload failed: " + xhr.statusText);
              }
            };
            
            xhr.onerror = function() {
              btn.disabled = false;
              btn.innerText = "Upload";
              hideLoader();
              showToast("Upload error occurred");
            };
            
            xhr.ontimeout = function() {
              btn.disabled = false;
              btn.innerText = "Upload";
              hideLoader();
              showToast("Upload timed out - file may be too large");
            };
            
            xhr.open('POST', '/upload');
            xhr.send(formData);
          });

          // Load files on page load
          document.addEventListener('DOMContentLoaded', loadFiles);
        </script>
      </body>
      </html>
    )rawliteral";

    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "text/html", html);
  });

  // Upload file handler with better error handling
  server.on("/upload", HTTP_POST, []() {
    addLog("Upload POST handler called");
    server.sendHeader("Location", "/");
    server.send(303);
  }, handleUpload);

  // Serve audio files or other SPIFFS files with better error handling
  server.onNotFound([]() {
    String path = server.uri();
    addLog("File requested: " + path + " from " + server.client().remoteIP().toString());

    if (!SPIFFS.exists(path)) {
      addLog("ERROR: File not found: " + path);
      server.send(404, "text/plain", "File not found: " + path);
      return;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) {
      addLog("ERROR: Could not open file: " + path);
      server.send(500, "text/plain", "Could not open file");
      return;
    }

    String contentType = getContentType(path);
    size_t fileSize = file.size();
    addLog("Serving file: " + path + " (" + String(fileSize) + " bytes, " + contentType + ")");
    
    server.sendHeader("Accept-Ranges", "bytes");
    server.sendHeader("Content-Length", String(fileSize));
    server.streamFile(file, contentType);
    file.close();
    
    addLog("File served successfully: " + path);
  });

  server.begin();
  addLog("✅ HTTP server started on port 80");
  addLog("Server ready - Free heap: " + String(ESP.getFreeHeap()));

  updateDuckDNS();
}

void loop() {
  server.handleClient();
  
  // Feed the watchdog
  yield();
  
  // Periodic status log (every 5 minutes)
  static unsigned long lastStatusLog = 0;
  if (millis() - lastStatusLog > 300000) {
    addLog("Status: Free heap=" + String(ESP.getFreeHeap()) + ", WiFi=" + 
           (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
    lastStatusLog = millis();
  }
  
  // Check WiFi connection periodically (every 30 seconds)
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      addLog("WiFi disconnected, attempting reconnection...");
      WiFi.begin(ssid, password);
    }
    lastWiFiCheck = millis();
  }
  
  delay(1); // Small delay to prevent tight looping
}
