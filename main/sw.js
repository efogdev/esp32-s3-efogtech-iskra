let cache_name = 'iskra'
let urls_to_cache = [
	'/',
	'/ota',
	'/generate_204',
]

self.addEventListener('install', (e) => {
	e.waitUntil(caches.open(cache_name).then((cache) => {
		return cache.addAll(urls_to_cache)
	}))
})

self.addEventListener('fetch', (e) => {
	e.respondWith(caches.match(e.request).then((response) => {
		if(response)
			return response
		else
			return fetch(e.request)
	}) )
})
