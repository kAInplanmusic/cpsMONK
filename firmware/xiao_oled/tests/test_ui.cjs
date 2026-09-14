// Browser integration against explicitly SYNTHETIC API data, NOT an ESP32 test.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const TARGET = 1000;
(async () => {
 const browser = await chromium.launch({headless:true});
 const page = await browser.newPage({viewport:{width:390,height:844}});
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 let state='idle',waveCalls=0,offline=false,level='good',levelReady=false;
 const status=()=>({
  state,count:state==='complete'?1000:state==='recording'?500:0,target:TARGET,seen:520,
  cps:150.2,shapeSimilarity:97.5,periodCvPercent:0.4,amplitudeCvPercent:2,
  clippedPercent:0,noiseRms:0.0001,threshold:0.001,rejected:0,
  timingWarning:false,clippingWarning:false,windowTruncated:false,
  provisionalCps:148,windowSeconds:7.1,error:state==='failed'?'SYNTHETIC test timeout':'',
  minCps:50,maxCps:220,thresholdMultiplier:6,thresholdFloor:0.001,
  level,levelAmplitude:level==='good'?0.045:level==='loud'?0.4:0.004,levelDb:-27,levelReady,
  levelLow:0.015,levelHigh:0.12,levelLoud:0.25,
  gateOpen:state!=='idle',autoStart:true,cpsGate:50,warmupSeconds:2,
  rawSamples:400000,sampleRate:32000,wavePoints:128,windowDurationMs:6});
 const waves={sampleRate:32000,wavePoints:128,windowDurationMs:6,count:1000,
  impacts:Array.from({length:1000},(_,i)=>({index:i+1,sample:32000+i*213,amplitude:0.1,similarity:97,
   waveform:Array.from({length:128},(_,j)=>Math.cos(j/3)*Math.exp(-Math.abs(j-64)/10))}))};
 const html=fs.readFileSync(path.join(__dirname,'../web/index.html'),'utf8');
 await page.route('http://cpsmonk.test/**',async route=>{
  const p=new URL(route.request().url()).pathname;
  if(p==='/')return route.fulfill({contentType:'text/html',body:html});
  if(offline)return route.abort();
  if(p==='/api/start'){state='calibrating';return route.fulfill({json:{ok:true}});}
  if(p==='/api/reset'||p==='/api/config'){state='idle';return route.fulfill({json:{ok:true}});}
  if(p==='/api/status')return route.fulfill({json:status()});
  if(p==='/api/waveforms'){waveCalls++;return route.fulfill({json:waves});}
  return route.fulfill({status:404,body:'not found'});
 });
 await page.goto('http://cpsmonk.test/');
 await page.waitForFunction(()=>!document.querySelector('#start').disabled===false||true);
 await page.waitForFunction(()=>document.querySelector('#levelText').innerText.length>1);
 // The level gate must keep the start button locked until the band was good.
 assert(await page.locator('#start').isDisabled(),'start locked before level was good');
 assert((await page.locator('#levelText').innerText()).includes('Abstand ok'),'level text shown');
 // The bar must scale against the overload threshold, not against full scale.
 const fill = await page.locator('#levelFill').evaluate(el=>parseFloat(el.style.width));
 assert(fill>10 && fill<30, 'bar scaled against the overload threshold, got '+fill+'%');
 const zone = await page.locator('#levelZone').evaluate(el=>[parseFloat(el.style.left),parseFloat(el.style.width)]);
 assert(zone[0]>0 && zone[0]<15 && zone[1]>30 && zone[1]<60, 'target zone drawn from band edges, got '+zone.join('/'));
 assert.equal(await page.locator('#cps').innerText(),'—');
 levelReady=true;
 await page.waitForFunction(()=>!document.querySelector('#start').disabled);
 await page.click('#start');
 await page.waitForFunction(()=>document.querySelector('#state').innerText.includes('Ruhekalibrierung'));
 assert(await page.locator('#start').isDisabled());
 state='waiting';
 await page.waitForFunction(()=>document.querySelector('#state').innerText.includes('Auto-Start'));
 assert((await page.locator('#count').innerText()).includes('Impulse erkannt'),'waiting shows seen strokes');
 state='warmup';
 await page.waitForFunction(()=>document.querySelector('#state').innerText.includes('Vorlauf'));
 assert((await page.locator('#count').innerText()).includes('148'),'warmup shows provisional CPS');
 state='recording';
 await page.waitForFunction(()=>document.querySelector('#count').innerText.includes('500'));
 assert.equal(await page.locator('#cps').innerText(),'—','metrics hidden while recording');
 state='complete';
 await page.waitForFunction(()=>!document.querySelector('#selected').disabled);
 assert.equal(await page.locator('#cps').innerText(),'150,20');
 assert(await page.locator('#wavDownload').isVisible());
 // The count is no longer a fixed target: selection must follow the real count.
 assert.equal(await page.locator('#selected').getAttribute('max'),'1000');
 await page.locator('#selected').fill('1000');await page.locator('#selected').dispatchEvent('input');
 assert((await page.locator('#selectedDetails').innerText()).includes('Impuls 1000'));
 await page.waitForTimeout(1200);assert.equal(waveCalls,1);
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'mobile layout');
 await page.screenshot({path:'/tmp/cpsmonk-ui-synthetic.png',fullPage:true});
 page.on('dialog',d=>d.accept());await page.click('#reset');
 await page.waitForFunction(()=>document.querySelector('#state').innerText.startsWith('Bereit'));
 await page.waitForFunction(()=>document.querySelector('#levelText').innerText.length>1);
 assert(await page.locator('#selected').isDisabled());
 // Reset clears the level gate: with the band back to quiet the start locks again.
 level='quiet';levelReady=false;
 await page.waitForFunction(()=>document.querySelector('#start').disabled);
 assert((await page.locator('#levelText').innerText()).includes('zu leise'));
 level='good';levelReady=true;
 state='complete';await page.waitForFunction(()=>!document.querySelector('#selected').disabled);
 assert.equal(waveCalls,2);
 offline=true;await page.waitForFunction(()=>document.querySelector('#connection').innerText.includes('nicht erreichbar'));
 assert(await page.locator('#start').isDisabled());
 offline=false;state='failed';await page.waitForFunction(()=>document.querySelector('#error').innerText.includes('SYNTHETIC'));
 assert(await page.locator('#wavDownload').isVisible());assert.equal(errors.length,0,errors.join('\n'));
 console.log('PASS browser: level gate, idle/calibration/waiting/warmup/recording/complete/reset/failed, 1000-wave overlay selection, count-based validation, WAV link, mobile layout, no JS errors. SYNTHETIC API only.');
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
