function resetESP32() {
    fetch("/reset", { method: "POST" })
      .then(() => alert("ESP32 is resetting..."));
  }
  
  function updateSSID(event) {
    event.preventDefault();
    const ssid = document.getElementById("ssid").value;
    fetch("/updateSSID", {
      method: "POST",
      body: new URLSearchParams({ ssid }),
    }).then(() => alert("SSID updated!"));
  }
  
  function updateAdminData() {
    fetch("/messages")
      .then((response) => response.json())
      .then((data) => {
        document.getElementById("admin-messages").innerHTML = data
          .map((msg) => `<div class="message">${msg}</div>`)
          .join("");
      });
  
    fetch("/help-requests")
      .then((response) => response.json())
      .then((data) => {
        document.getElementById("admin-help-requests").innerHTML = data
          .map((req) => `<div class="help-request">${req}</div>`)
          .join("");
      });
  }
  
  setInterval(updateAdminData, 1000);