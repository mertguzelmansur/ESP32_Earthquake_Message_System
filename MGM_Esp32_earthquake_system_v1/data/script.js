function sendMessage(event) {
    event.preventDefault();
    const user = document.getElementById("user").value;
    const message = document.getElementById("message").value;
    fetch("/send", {
      method: "POST",
      body: new URLSearchParams({ user, message }),
    });
  }
  
  function requestHelp() {
    const user = prompt("Enter your name:");
    if (user) {
      fetch("/help", {
        method: "POST",
        body: new URLSearchParams({ user }),
      });
    }
  }
  
  function updateMessages() {
    fetch("/messages")
      .then((response) => response.json())
      .then((data) => {
        document.getElementById("messages").innerHTML = data
          .map((msg) => `<div class="message">${msg}</div>`)
          .join("");
      });
  }
  
  function updateHelpRequests() {
    fetch("/help-requests")
      .then((response) => response.json())
      .then((data) => {
        document.getElementById("help-requests").innerHTML = data
          .map((req) => `<div class="help-request">${req}</div>`)
          .join("");
      });
  }
  
  setInterval(updateMessages, 1000);
  setInterval(updateHelpRequests, 1000);