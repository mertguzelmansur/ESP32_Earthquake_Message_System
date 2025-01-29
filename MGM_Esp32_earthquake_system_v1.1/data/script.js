let userLocation = null;

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
      const helpData = {
        user: user,
        location: userLocation
      };
      
      fetch("/help", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(helpData)
      })
      .then(() => {
        alert("Help request sent! Emergency services will be notified.");
        updateHelpRequests(); // Immediately update the display
      })
      .catch(err => alert("Failed to send help request"));
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
                .map((req) => `<div class="help-request">
                    <i class="fas fa-exclamation-circle"></i> ${req}
                    ${userLocation ? '<i class="fas fa-map-marker-alt"></i> Location shared' : ''}
                </div>`)
                .join("");
        })
        .catch(err => console.error("Failed to update help requests:", err));
  }
  
  function updateStats() {
    fetch("/system-stats")
        .then(response => response.json())
        .then(data => {
            document.getElementById("connected-users").textContent = data.connectedUsers;
            document.getElementById("help-count").textContent = data.activeHelpRequests;
            document.getElementById("signal-strength").textContent = data.signalStrength + '%';
        })
        .catch(err => console.error("Failed to update stats:", err));
  }
  
  function shareLocation() {
    if ("geolocation" in navigator) {
        navigator.geolocation.getCurrentPosition(function(position) {
            userLocation = {
                lat: position.coords.latitude,
                lng: position.coords.longitude
            };
            
            fetch("/update-location", {
                method: "POST",
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(userLocation)
            })
            .then(() => alert("Location shared successfully!"))
            .catch(err => alert("Failed to share location"));
        }, function(error) {
            alert("Error getting location: " + error.message);
        });
    } else {
        alert("Geolocation is not supported by your browser");
    }
  }
  
  setInterval(updateMessages, 1000);
  setInterval(updateHelpRequests, 1000);
  setInterval(updateStats, 5000);