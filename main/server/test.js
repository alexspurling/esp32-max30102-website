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
wglp.gOffsetY = 0;

redBuffer = [];
irBuffer = [];

var droppedFrames = 0;

var lastRed = 0;
var lastIr = 0;
var frameCount = 0;
var targetScaleY = 1;

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
  const increment = 0.00001;
  if (wglp.gScaleY !== targetScaleY) {
    // normal: 0.00025
    // very tiny: 0.1
    // very large: 0.000009
    var diff = wglp.gScaleY - targetScaleY;
    if (Math.abs(diff) < increment) {
      wglp.gScaleY = targetScaleY;
    } else {
      var newScale = wglp.gScaleY - (diff / 10);
      wglp.gScaleY = newScale;
    }
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
        redBuffer.push(parseFloat(data[i]));
        irBuffer.push(parseFloat(data[i + 1]));
      }
      var maxValue = -Infinity;
      var minValue = Infinity;
      for (var i = redline.numPoints / 2; i < redline.numPoints; i++) {
        if (redline.getY(i) > maxValue) {
          maxValue = redline.getY(i);
        }
        if (redline.getY(i) < minValue) {
          minValue = redline.getY(i);
        }
        if (irline.getY(i) > maxValue) {
          maxValue = irline.getY(i);
        }
        if (irline.getY(i) < minValue) {
          minValue = irline.getY(i);
        }
      }
      document.getElementById("min").innerHTML = minValue;
      document.getElementById("max").innerHTML = maxValue;
      var range = maxValue - minValue;
      if (range > 0) {
        targetScaleY = 1 / range;
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
