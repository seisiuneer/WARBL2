//
// Service worker for WARBL configuration tool offline use resource caching
//
//
//
//
// Updated 19 September 2024 at 0700
//
//
//
//

const cacheName = "warbl_3";

const contentToCache = [
"configure.html",
"favicon.ico",
"manifest.json",
"css/config.min.css",
"css/nouislider.css",
"css/modal.css", 
"js/jquery-3.3.1.min.js",
"js/nouislider.js",
"js/WebAudioFontPlayer.js",
"js/0650_SBLive_sf2.js",
"js/constants.min.js",
"js/midi.min.js",
"js/d3.min.js",
"js/daypilot-modal.min-3.10.1.js",
"js/update.js",
"img/small_logo.png",
"img/downarrow.png",
"img/info-circle.svg",
"img/volume-off.svg",
"img/volume-up.svg",
"img/angle-up.svg",
"img/angle-down.svg"
];

// Installing Service Worker
self.addEventListener("install", (e) => {

    console.log("[Service Worker] Install");

    // Make this the current service worker
    self.skipWaiting();
    
    e.waitUntil((async () => {
      const cache = await caches.open(cacheName);
      console.log("[Service Worker] Caching all: app shell and content");
      await cache.addAll(contentToCache);
      console.log("[Service Worker] Cache addAll complete!");
    })());


  });

self.addEventListener("activate", event => {

    console.log("[Service Worker] Activate event");

    clients.claim().then(() => {
        //claim means that the html file will use this new service worker.
        console.log(
          "[Service Worker] - The service worker has now claimed all pages so they use the new service worker."
        );
    });

   event.waitUntil(
        caches.keys().then((keys) => {
          return Promise.all(
            keys.filter((key) => key != cacheName).map((key) => {if (key.indexOf("warbl") != -1){caches.delete(key)}})
          );
        })
    );

});
  
// Fetching content using Service Worker
self.addEventListener("fetch", (e) => {

    //console.log(`[Service Worker] fetching: ${e.request.url}`);

    // Cache http and https only, skip unsupported chrome-extension:// and file://...
    if (!(
        e.request.url.startsWith("http:") || e.request.url.startsWith("https:")
    )) {
        return; 
    }

    e.respondWith((async () => {

        const r = await caches.match(e.request,{ignoreSearch: true});

        if (r){

            //console.log(`[Service Worker] Returning cached resource: ${e.request.url}`);

            return r;
        }

        try{

            //console.log(`[Service Worker] Fetching resource: ${e.request.url}`);

            const response = await fetch(e.request);

            //Don"t cache any resources

            // if ((e.request.url.indexOf("service_worker") == -1) && (e.request.url.indexOf("soundfonts") == -1)){
            
            //     const cache = await caches.open(cacheName);

            //     console.log(`[Service Worker] Caching new resource: ${e.request.url}`);

            //     cache.put(e.request, response.clone());

            // }
 
            return response;
            
        }
        catch (error){

            //console.log("[Service Worker] fetch error: "+error);
    
        }
    })());
});


