#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>

// Wi-Fi credentials
const char* ssid = "";
const char* password = "";

// Static IP config
IPAddress local_IP(192, 168, 1, 6);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);    // Google DNS
IPAddress secondaryDNS(8, 8, 4, 4);  // Google DNS

// DuckDNS credentials
const char* duckdns_token = "";
const char* duckdns_domain = "";

// SD Card pins
#define SD_CS 5  // Chip Select pin for SD card

WebServer server(80);

// Logging system
String debugLogs = "";
const int MAX_LOG_SIZE = 10000; // Limit log size to prevent memory issues

// DuckDNS retry configuration
const int MAX_DUCKDNS_RETRIES = 3;
const int DUCKDNS_RETRY_DELAY = 5000; // 5 seconds between retries

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

String getPublicIP() {
  addLog("Getting public IP from ipinfo.io...");
  WiFiClient client;
  if (client.connect("ipinfo.io", 80)) {
    addLog("Connected to ipinfo.io");
    client.println("GET /ip HTTP/1.1");
    client.println("Host: ipinfo.io");
    client.println("Connection: close");
    client.println();
    
    unsigned long timeout = millis() + 10000;
    while (client.available() == 0 && millis() < timeout) {
      delay(50);
      yield();
    }
    
    if (client.available()) {
      String response = client.readString();
      addLog("Public IP response: " + response);
      
      // Find the IP address in the response
      int ipStart = response.indexOf("\r\n\r\n");
      if (ipStart > 0) {
        String ip = response.substring(ipStart + 4);
        ip.trim(); // Remove any whitespace
        addLog("Extracted IP: " + ip);
        client.stop();
        return ip;
      }
    }
    client.stop();
  }
  
  // Fallback to another service if ipinfo.io fails
  addLog("Trying fallback service (icanhazip.com)...");
  if (client.connect("icanhazip.com", 80)) {
    addLog("Connected to icanhazip.com");
    client.println("GET / HTTP/1.1");
    client.println("Host: icanhazip.com");
    client.println("Connection: close");
    client.println();
    
    unsigned long timeout = millis() + 10000;
    while (client.available() == 0 && millis() < timeout) {
      delay(50);
      yield();
    }
    
    if (client.available()) {
      String response = client.readString();
      addLog("Fallback IP response: " + response);
      
      // Find the IP address in the response
      int ipStart = response.indexOf("\r\n\r\n");
      if (ipStart > 0) {
        String ip = response.substring(ipStart + 4);
        ip.trim(); // Remove any whitespace
        addLog("Extracted IP: " + ip);
        client.stop();
        return ip;
      }
    }
    client.stop();
  }
  
  addLog("Failed to get public IP from both services");
  return "";
}

bool updateDuckDNS() {
  addLog("Starting DuckDNS update...");
  String publicIP = getPublicIP();
  if (publicIP == "") {
    addLog("❌ Could not get public IP, using local IP");
    publicIP = WiFi.localIP().toString();
  }
  
  addLog("Current IP: " + publicIP);
  addLog("DuckDNS Domain: " + String(duckdns_domain));
  
  for (int attempt = 1; attempt <= MAX_DUCKDNS_RETRIES; attempt++) {
    addLog("Attempt " + String(attempt) + " of " + String(MAX_DUCKDNS_RETRIES));
    
    WiFiClient client;
    addLog("Attempting to connect to DuckDNS server...");
    
    if (client.connect("www.duckdns.org", 80)) {
      addLog("Connected to DuckDNS server");
      
      String request = "GET /update?domains=" + String(duckdns_domain) + "&token=" + String(duckdns_token) + "&ip=" + publicIP + " HTTP/1.1\r\n";
      request += "Host: www.duckdns.org\r\n";
      request += "Connection: close\r\n\r\n";
      
      addLog("Sending request: " + request);
      client.print(request);
      
      // Wait for response with watchdog feeding
      unsigned long timeout = millis() + 10000; // 10 second timeout
      addLog("Waiting for response...");
      
      while (client.available() == 0 && millis() < timeout) {
        delay(50);
        yield(); // Feed the watchdog
      }
      
      if (client.available()) {
        String response = client.readString();
        addLog("Received response: " + response.substring(0, 100));
        
        if (response.indexOf("OK") > 0) {
          addLog("✅ DuckDNS Update successful");
          addLog("Public IP: " + publicIP);
          client.stop();
          return true;
        } else if (response.indexOf("KO") > 0) {
          addLog("❌ DuckDNS Update failed - Invalid token or domain");
        } else {
          addLog("❌ DuckDNS Update failed - Unknown response: " + response.substring(0, 100));
        }
      } else {
        addLog("❌ DuckDNS Update timeout - No response received");
      }
      
      addLog("Closing connection");
      client.stop();
    } else {
      addLog("❌ DuckDNS connection failed");
      addLog("WiFi Status: " + String(WiFi.status()));
      addLog("RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    
    if (attempt < MAX_DUCKDNS_RETRIES) {
      addLog("Waiting " + String(DUCKDNS_RETRY_DELAY/1000) + " seconds before retry...");
      delay(DUCKDNS_RETRY_DELAY);
    }
  }
  
  addLog("❌ All DuckDNS update attempts failed");
  return false;
}

// Handle file upload (via POST /upload)
void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    addLog("Upload Start: " + filename + " (Size will be determined)");
    
    if (SD.exists(filename)) {
      SD.remove(filename);
      addLog("Removed existing file: " + filename);
    }
    
    uploadFile = SD.open(filename, FILE_WRITE);
    if (!uploadFile) {
      addLog("ERROR: Failed to open file for writing: " + filename);
      server.send(500, "text/plain", "Failed to create file");
      return;
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      addLog("Upload chunk written: " + String(written) + "/" + String(upload.currentSize) + " bytes");
      if (written != upload.currentSize) {
        addLog("ERROR: Write size mismatch!");
      }
    } else {
      addLog("ERROR: File handle not available for writing");
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      addLog("Upload completed: " + upload.filename + " (" + String(upload.totalSize) + " bytes)");
    }
    
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    addLog("Upload aborted: " + upload.filename);
    if (uploadFile) {
      uploadFile.close();
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
  addLog("ESP32 Chip ID: " + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX));
  addLog("CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz");

  // Initialize SD card
  if (!SD.begin(SD_CS)) {
    addLog("❌ SD Card Mount Failed");
    addLog("SD Card CS Pin: " + String(SD_CS));
    return;
  } else {
    addLog("✅ SD Card mounted successfully");
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      addLog("❌ No SD card attached");
      return;
    }
    addLog("SD Card Type: " + String(cardType));
    addLog("SD Card Size: " + String(SD.cardSize() / (1024 * 1024)) + " MB");
  }

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    addLog("❌ STA Failed to configure static IP and DNS");
    addLog("Attempting to continue with DHCP...");
  } else {
    addLog("✅ Static IP and DNS configured successfully");
  }

  addLog("Connecting to WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
    delay(500);
    wifiAttempts++;
    if (wifiAttempts % 10 == 0) {
      addLog("WiFi connection attempt: " + String(wifiAttempts));
      addLog("WiFi Status: " + String(WiFi.status()));
      addLog("RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    addLog("✅ Connected to WiFi");
    addLog("IP Address: " + WiFi.localIP().toString());
    addLog("Gateway: " + WiFi.gatewayIP().toString());
    addLog("Subnet Mask: " + WiFi.subnetMask().toString());
    addLog("DNS: " + WiFi.dnsIP().toString());
    addLog("RSSI: " + String(WiFi.RSSI()) + " dBm");
    addLog("Channel: " + String(WiFi.channel()));
  } else {
    addLog("❌ Failed to connect to WiFi");
    addLog("Last WiFi Status: " + String(WiFi.status()));
  }

  // Add a test endpoint
  server.on("/test", HTTP_GET, []() {
    String clientIP = server.client().remoteIP().toString();
    int clientPort = server.client().remotePort();
    int serverPort = server.client().localPort();
    
    addLog("Test endpoint called from: " + clientIP + ":" + String(clientPort));
    addLog("Server port: " + String(serverPort));
    
    String response = "Server is running on port " + String(serverPort) + "\n";
    response += "Client IP: " + clientIP + "\n";
    response += "Client Port: " + String(clientPort) + "\n";
    response += "Server Port: " + String(serverPort) + "\n";
    response += "Time: " + String(millis() / 1000) + " seconds\n";
    
    server.send(200, "text/plain", response);
  });

  // Serve file list as JSON with better error handling
  server.on("/list", HTTP_GET, []() {
    addLog("File list requested");
    String fileList = "[";
    File root = SD.open("/");
    if (!root) {
      addLog("ERROR: Failed to open SD root");
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

  // Serve audio files or other SD files with better error handling
  server.onNotFound([]() {
    String path = server.uri();
    addLog("File requested: " + path + " from " + server.client().remoteIP().toString());

    if (!SD.exists(path)) {
      addLog("ERROR: File not found: " + path);
      server.send(404, "text/plain", "File not found: " + path);
      return;
    }

    File file = SD.open(path, "r");
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

  // Wait for network to stabilize before first DuckDNS update
  addLog("Waiting 10 seconds before first DuckDNS update...");
  delay(10000);
  updateDuckDNS();
}

void loop() {
  server.handleClient();
  
  // Feed the watchdog
  yield();
  
  // Periodic status log (every 5 minutes)
  static unsigned long lastStatusLog = 0;
  if (millis() - lastStatusLog > 300000) {
    addLog("=== Status Update ===");
    addLog("Free heap: " + String(ESP.getFreeHeap()));
    addLog("WiFi Status: " + String(WiFi.status()));
    addLog("RSSI: " + String(WiFi.RSSI()) + " dBm");
    addLog("IP Address: " + WiFi.localIP().toString());
    addLog("Uptime: " + String(millis() / 1000) + " seconds");
    lastStatusLog = millis();
  }
  
  // Check WiFi connection periodically (every 30 seconds)
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      addLog("WiFi disconnected, attempting reconnection...");
      addLog("Last WiFi Status: " + String(WiFi.status()));
      WiFi.begin(ssid, password);
    }
    lastWiFiCheck = millis();
  }
  
  // Update DuckDNS periodically (every 5 minutes)
  static unsigned long lastDuckDNSUpdate = 0;
  if (millis() - lastDuckDNSUpdate > 300000) {
    addLog("=== DuckDNS Update ===");
    updateDuckDNS();
    lastDuckDNSUpdate = millis();
  }
  
  delay(1); // Small delay to prevent tight looping
}
