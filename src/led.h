#pragma once

// Status LED (the DevKit's RGB LED). A small task shows, at a glance:
//   colour     worst of PM2.5 est / PM1.0 / PM10 on the portal's scale:
//              green good -> yellow -> orange -> red -> purple hazardous
//   brightness follows the room light (barely on in a dark room)
//   patterns   rainbow             starting up (no reading yet)
//              breathing colour    dust sensor warming up for a sample
//              two white flashes   a sample was just logged
//              magenta blink / 2 s offline (setup AP on)
// Off entirely with `led off` (saved).

void ledBegin();
void ledSampleTaken();  // called by the sampler after each logged sample
