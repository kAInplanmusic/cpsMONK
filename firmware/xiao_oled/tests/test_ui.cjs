// Browser integration against explicitly SYNTHETIC API data, NOT an ESP32 test.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
(async () => {
 const browser = await chromium.launch({headless:true});
 const page = await browser.newPage({viewport:{width:390,height:844}});
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 let state='idle',waveCalls=0,offline=false;
 const status=()=>({state,count:state==='complete'?500:state==='recording'?250:0,target:500,cps:85,shapeSimilarity:98,periodCvPercent:0.4,amplitudeCvPercent:2,clippedPercent:0,noiseRms:0.0001,threshold:0.001,rejected:0,timingWarning:false,clippingWarning:false,error:state==='failed'?'SYNTHETIC test timeout':'',minCps:20,maxCps:200,thresholdMultiplier:6,thresholdFloor:0.001,rawSamples:200000});
 const waves={sampleRate:32000,wavePoints:128,windowDurationMs:3,impacts:Array.from({length:500},(_,i)=>({index:i+1,sample:32000+i*376,amplitude:0.1,similarity:98,waveform:Array.from({length:128},(_,j)=>Math.cos(j/3)*Math.exp(-Math.abs(j-64)/10))}))};
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
 await page.waitForFunction(()=>!document.querySelector('#start').disabled);
 assert.equal(await page.locator('#cps').innerText(),'—');
 await page.click('#start');await page.waitForFunction(()=>document.querySelector('#state').innerText.includes('Ruhekalibrierung'));
 assert(await page.locator('#start').isDisabled());
 state='recording';await page.waitForFunction(()=>document.querySelector('#count').innerText.includes('250'));
 assert.equal(await page.locator('#cps').innerText(),'—');
 state='complete';await page.waitForFunction(()=>!document.querySelector('#selected').disabled);
 assert.equal(await page.locator('#cps').innerText(),'85,00');assert(await page.locator('#wavDownload').isVisible());
 await page.locator('#selected').fill('500');await page.locator('#selected').dispatchEvent('input');
 assert((await page.locator('#selectedDetails').innerText()).includes('Impuls 500'));
 await page.waitForTimeout(1200);assert.equal(waveCalls,1);
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.screenshot({path:'/tmp/cpsmonk-ui-synthetic.png',fullPage:true});
 page.on('dialog',d=>d.accept());await page.click('#reset');await page.waitForFunction(()=>document.querySelector('#state').innerText.startsWith('Bereit'));
 assert(await page.locator('#selected').isDisabled());
 state='complete';await page.waitForFunction(()=>!document.querySelector('#selected').disabled);assert.equal(waveCalls,2);
 offline=true;await page.waitForFunction(()=>document.querySelector('#connection').innerText.includes('nicht erreichbar'));
 assert(await page.locator('#start').isDisabled());
 offline=false;state='failed';await page.waitForFunction(()=>document.querySelector('#error').innerText.includes('SYNTHETIC'));
 assert(await page.locator('#wavDownload').isVisible());assert.equal(errors.length,0,errors.join('\n'));
 console.log('PASS browser: idle/calibration/recording/complete/reset/new-run/offline/recovery, 500-wave overlay selection, WAV link, mobile layout, no JS errors. SYNTHETIC API only.');
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
