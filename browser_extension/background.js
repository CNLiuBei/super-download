// Super Download Bridge - Background Service Worker
// Intercepts browser downloads and sends them to Super Download
// via WebSocket on ws://127.0.0.1:18615

const WS_URL = "ws://127.0.0.1:18615";

// File extensions to intercept
const INTERCEPT_EXTENSIONS = new Set([
  // Archives
  "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso", "dmg",
  // Executables
  "exe", "msi", "deb", "rpm", "apk", "appimage",
  // Media
  "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm",
  "mp3", "flac", "wav", "aac", "ogg", "wma",
  // Documents
  "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx",
  // Disk images
  "img", "bin", "rom",
  // Torrents
  "torrent",
]);

// Minimum file size to intercept (1 MB) - skip tiny files
const MIN_SIZE_BYTES = 1 * 1024 * 1024;

let ws = null;
let wsConnected = false;

function connectWebSocket() {
  if (ws && wsConnected) return;

  try {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      wsConnected = true;
      console.log("[Super Download] Connected");
    };

    ws.onclose = () => {
      wsConnected = false;
      ws = null;
      // Retry connection after 5 seconds
      setTimeout(connectWebSocket, 5000);
    };

    ws.onerror = () => {
      wsConnected = false;
    };

    ws.onmessage = (event) => {
      console.log("[Super Download] Response:", event.data);
    };
  } catch (e) {
    wsConnected = false;
    setTimeout(connectWebSocket, 5000);
  }
}

function shouldIntercept(item) {
  const url = item.url || "";
  if (!url.startsWith("http://") && !url.startsWith("https://")) return false;

  // Check file extension
  let path = new URL(url).pathname.toLowerCase();
  const ext = path.split(".").pop();
  if (INTERCEPT_EXTENSIONS.has(ext)) return true;

  // Check file size if known
  if (item.fileSize && item.fileSize >= MIN_SIZE_BYTES) return true;

  // Check Content-Disposition style filename
  if (item.filename) {
    const fnExt = item.filename.split(".").pop().toLowerCase();
    if (INTERCEPT_EXTENSIONS.has(fnExt)) return true;
  }

  return false;
}

function sendToManager(url, filename, referrer, cookie) {
  if (!wsConnected || !ws) {
    // WebSocket not connected — fall back to superdownload:// protocol
    launchViaProtocol(url, referrer, cookie);
    // Also try reconnecting WS for future downloads
    connectWebSocket();
    return;
  }

  ws.send(JSON.stringify({ url, filename, referrer, cookie }));
}

// Launch Super Download via custom protocol URL.
// Injects a script into the active tab to create a hidden iframe with the
// protocol URL. This works because the navigation happens in a normal web
// page context where the OS protocol handler is honoured.
function launchViaProtocol(url, referrer, cookie) {
  const params = new URLSearchParams();
  params.set("url", url);
  if (referrer) params.set("referer", referrer);
  if (cookie) params.set("cookie", cookie);
  const protocolUrl = "superdownload://download?" + params.toString();

  chrome.tabs.query({ active: true, currentWindow: true }, (tabs) => {
    if (!tabs || tabs.length === 0) return;
    const tabId = tabs[0].id;

    // Inject a content script that creates a hidden iframe to trigger the protocol
    chrome.scripting.executeScript({
      target: { tabId: tabId },
      func: (pUrl) => {
        const iframe = document.createElement("iframe");
        iframe.style.display = "none";
        iframe.src = pUrl;
        document.body.appendChild(iframe);
        // Clean up after 3 seconds
        setTimeout(() => iframe.remove(), 3000);
      },
      args: [protocolUrl]
    }).catch((err) => {
      console.warn("[Super Download] Script injection failed, trying tabs.update:", err);
      // Fallback: navigate the tab directly (will show protocol prompt)
      chrome.tabs.update(tabId, { url: protocolUrl });
    });
  });
}

// Listen for new downloads
chrome.downloads.onCreated.addListener((item) => {
  if (!shouldIntercept(item)) return;

  // Cancel the browser download
  chrome.downloads.cancel(item.id, () => {
    chrome.downloads.erase({ id: item.id });
  });

  // Get cookies for the download URL and send to our manager
  const downloadUrl = item.url;
  const filename = item.filename || "";
  const referrer = item.referrer || "";

  try {
    new URL(downloadUrl); // validate URL
    chrome.cookies.getAll({ url: downloadUrl }, (cookies) => {
      let cookieStr = "";
      if (cookies && cookies.length > 0) {
        cookieStr = cookies.map(c => c.name + "=" + c.value).join("; ");
      }
      sendToManager(downloadUrl, filename, referrer, cookieStr);
    });
  } catch (e) {
    // URL parse failed, send without cookies
    sendToManager(downloadUrl, filename, referrer, "");
  }
});

// Connect on startup
connectWebSocket();
