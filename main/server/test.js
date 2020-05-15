document.getElementById("test").innerHTML = "WebSocket is not connected";

var websocket = new WebSocket('ws://192.168.1.44/');

const canv = document.getElementById("my_canvas");
const devicePixelRatio = window.devicePixelRatio || 1;
canv.width  = 900 / devicePixelRatio;
canv.height = 300 / devicePixelRatio;
const numX = Math.round(canv.clientWidth * devicePixelRatio);
console.log(devicePixelRatio);
const color = new graphs.ColorRGBA(Math.random(), Math.random(), Math.random(), 1);
const redline = new graphs.WebglLine(new graphs.ColorRGBA(200, 0, 0, 1), numX);
const irline = new graphs.WebglLine(new graphs.ColorRGBA(0, 0, 0, 1), numX);
const wglp = new graphs.WebGLPlot(canv);

redline.lineSpaceX(-1, 2 / numX);
irline.lineSpaceX(-1, 2 / numX);
wglp.addLine(redline);
wglp.addLine(irline);
wglp.gScaleY = 0.002;
wglp.gOffsetY = -0.5;

redBuffer = [];
irBuffer = [];

function newFrame() {
  if (redBuffer.length !== 1) {
    console.log("Buffer lens: ", redBuffer.length, irBuffer.length);
  }
  redline.shiftAdd(redBuffer);
  irline.shiftAdd(irBuffer);
  wglp.update();
  redBuffer.length = 0;
  irBuffer.length = 0;
  window.requestAnimationFrame(newFrame);
}
window.requestAnimationFrame(newFrame);

websocket.onopen = function(evt) {
  console.log('WebSocket connection opened');
  websocket.send("It's open! Hooray!!!");
  document.getElementById("test").innerHTML = "WebSocket is connected!";
}

websocket.onmessage = function(evt) {
  var msg = evt.data;
  var value;
  switch(msg.charAt(0)) {
    case 'L':
      console.log(msg);
      value = parseInt(msg.replace(/[^0-9\.]/g, ''), 10);
      slider.value = value;
      console.log("Led = " + value);
      break;
    default:
      document.getElementById("output").innerHTML = evt.data;
      var data = evt.data.split(",");
      redBuffer.push(parseFloat(data[0]));
      irBuffer.push(parseFloat(data[1]));
      break;
  }
}

websocket.onclose = function(evt) {
  console.log('Websocket connection closed');
  document.getElementById("test").innerHTML = "WebSocket closed";
}

websocket.onerror = function(evt) {
  console.log('Websocket error: ' + evt);
  document.getElementById("test").innerHTML = "WebSocket error????!!!1!!";
}
