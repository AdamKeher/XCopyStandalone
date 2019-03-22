var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);

connection.onopen = function () {
  connection.send('Connect ' + new Date());
};

connection.onerror = function (error) {
  console.log('WebSocket Error ', error);
};

connection.onmessage = function (e) {
  console.log('Server: ', e.data);
  document.getElementById('pinStatus').innerHTML = e.data;
};

connection.onclose = function () {
  console.log('WebSocket connection closed');
};

function sendChime() {
  connection.send("chime");
}