let accelerometerChart = null;

function initChart() {
    const ctx = document.getElementById('accelerometer-chart').getContext('2d');
    accelerometerChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'X-axis',
                data: [],
                borderColor: 'rgb(255, 99, 132)',
                tension: 0.1
            }, {
                label: 'Y-axis',
                data: [],
                borderColor: 'rgb(54, 162, 235)',
                tension: 0.1
            }, {
                label: 'Z-axis',
                data: [],
                borderColor: 'rgb(75, 192, 192)',
                tension: 0.1
            }]
        },
        options: {
            responsive: true,
            scales: {
                y: {
                    beginAtZero: true
                }
            }
        }
    });
}

function updateSystemStats() {
    fetch("/system-stats")
        .then(response => response.json())
        .then(data => {
            document.getElementById("system-uptime").textContent = formatUptime(data.uptime);
            document.getElementById("free-memory").textContent = formatMemory(data.freeMemory);
            document.getElementById("connected-devices").textContent = data.connectedDevices;
        })
        .catch(err => console.error("Failed to update system stats:", err));
}

function updateAccelerometerData() {
    fetch("/sensor-data")
        .then(response => response.json())
        .then(data => {
            updateChart(data);
            document.getElementById("sensor-data").innerHTML = `
                <div>X: ${data.x.toFixed(2)}g</div>
                <div>Y: ${data.y.toFixed(2)}g</div>
                <div>Z: ${data.z.toFixed(2)}g</div>
            `;
        })
        .catch(err => console.error("Failed to update sensor data:", err));
}

function updateChart(data) {
    const maxDataPoints = 50;
    
    accelerometerChart.data.labels.push(new Date().toLocaleTimeString());
    accelerometerChart.data.datasets[0].data.push(data.x);
    accelerometerChart.data.datasets[1].data.push(data.y);
    accelerometerChart.data.datasets[2].data.push(data.z);

    if (accelerometerChart.data.labels.length > maxDataPoints) {
        accelerometerChart.data.labels.shift();
        accelerometerChart.data.datasets.forEach(dataset => dataset.data.shift());
    }

    accelerometerChart.update();
}

function formatUptime(seconds) {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return `${hours}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
}

function formatMemory(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(2) + ' KB';
    return (bytes / 1048576).toFixed(2) + ' MB';
}

function toggleEmergencyMode() {
    fetch("/toggle-emergency", { method: "POST" })
        .then(response => response.json())
        .then(data => {
            alert(data.enabled ? "Emergency Mode Enabled" : "Emergency Mode Disabled");
        });
}

function clearAllData() {
    if (confirm("Are you sure you want to clear all data? This cannot be undone.")) {
        fetch("/clear-data", { method: "POST" })
            .then(() => {
                alert("All data cleared successfully");
                updateAdminData();
            });
    }
}

function clearMessages() {
    if (confirm("Clear all messages?")) {
        fetch("/clear-messages", { method: "POST" })
            .then(() => {
                document.getElementById("admin-messages").innerHTML = "";
                alert("Messages cleared");
            });
    }
}

function clearHelpRequests() {
    if (confirm("Clear all help requests?")) {
        fetch("/clear-help-requests", { method: "POST" })
            .then(() => {
                document.getElementById("admin-help-requests").innerHTML = "";
                alert("Help requests cleared");
            });
    }
}

function updateThreshold(event) {
    event.preventDefault();
    const threshold = document.getElementById("threshold").value;
    fetch("/update-threshold", {
        method: "POST",
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ threshold: parseFloat(threshold) })
    })
    .then(() => alert("Earthquake sensitivity threshold updated"));
}

function updateAdminData() {
    // Update messages
    fetch("/messages")
        .then((response) => response.json())
        .then((data) => {
            document.getElementById("admin-messages").innerHTML = data
                .map((msg) => `<div class="message">
                    <i class="fas fa-comment"></i> ${msg}
                </div>`)
                .join("");
        })
        .catch(err => console.error("Failed to update messages:", err));

    // Update help requests
    fetch("/help-requests")
        .then((response) => response.json())
        .then((data) => {
            document.getElementById("admin-help-requests").innerHTML = data
                .map((req) => `<div class="help-request">
                    <i class="fas fa-exclamation-triangle"></i> ${req}
                </div>`)
                .join("");
        })
        .catch(err => console.error("Failed to update help requests:", err));

    // Update locations
    fetch("/locations")
        .then((response) => response.json())
        .then((data) => {
            const locationsList = data.map(loc => 
                `<div class="location-item">
                    <i class="fas fa-map-marker-alt"></i> 
                    ${loc.user}: ${loc.lat.toFixed(6)}, ${loc.lng.toFixed(6)}
                </div>`
            ).join("");
            
            if (document.getElementById("locations-list")) {
                document.getElementById("locations-list").innerHTML = locationsList;
            }
        })
        .catch(err => console.error("Failed to update locations:", err));
}

// Initialize chart when page loads
document.addEventListener('DOMContentLoaded', initChart);

// Set up periodic updates
setInterval(updateSystemStats, 1000);
setInterval(updateAccelerometerData, 500);
setInterval(updateAdminData, 1000);