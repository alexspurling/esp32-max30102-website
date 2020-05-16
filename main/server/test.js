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
wglp.gScaleY = 0.0005;
wglp.gOffsetY = -0.5;

redBuffer = [];
irBuffer = [];

var droppedFrames = 0;

var lastRed = 0;
var lastIr = 0;
var frameCount = 0;

function addPoint() {
  var red = 0;
  var ir = 0;
  document.getElementById("bufferLength").innerHTML = redBuffer.length;
  if (redBuffer.length > 0 && irBuffer.length > 0) {
    red = redBuffer.shift();
    ir = irBuffer.shift();
    lastRed = red;
    lastIr = ir;
    redline.shiftAdd([red]);
    irline.shiftAdd([ir]);
  } else {
    droppedFrames += 1;
    document.getElementById("droppedFrames").innerHTML = droppedFrames;
    // red = lastRed;
    // ir = lastIr;
  }
}

function newFrame() {
  addPoint();
  if (frameCount % 3 !== 0) {
    addPoint();
  }
  frameCount++;
  wglp.update();
  // redBuffer.length = 0;
  // irBuffer.length = 0;
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
      // document.getElementById("output").innerHTML = evt.data;
      var data = evt.data.split(",");
      // console.log(data.length);
      for (var i = 0; i < data.length-1; i+=2) {
        var red = parseFloat(data[i]);
        var ir = parseFloat(data[i + 1]);
        // if (isNaN(red)) {
        //   console.log("Red is NaN at idx " + i + ". Str was: ", data);
        // }
        redBuffer.push(red);
        irBuffer.push(ir);
      }
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
