'use strict';
const echo = require('./build/Release/echo_server');
//const echo = require('./build/Debug/echo_server');

echo.start(3000);

console.log("Echo listening on port 3000.");
