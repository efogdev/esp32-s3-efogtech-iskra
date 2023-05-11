import { h, render } from 'preact'
import './style'

const OFFLINE_THRESHOLD_MS = 1000
const STABILITY_THRESHOLD_MS = 16000
const STABILITY_THRESHOLD_DEGREES = 2

let interval = -1

class API {
	ws = null

	constructor() {
		this.tryInit()

		window.addEventListener('message', e => {
			if (e.data === 'ota_done') {
				window.emitter.emit('send', 'reboot')
				window.close()
			}
		})
	}

	close() {
		this.ws.close()
	}

	initSW() {
		try {
			navigator.serviceWorker.register('/sw.js')
				.catch(e => console.log(e))
		} catch (e) {
			console.log(e)
		}
	}

	tryInit() {
		try {
			this.init()
			this.initSW()
		} catch (e) {
			setTimeout(() => {
				this.tryInit()
			}, 5000)
		}
	}

	init() {
		clearInterval(interval)

		try {
			this.ws = new WebSocket("ws://192.168.4.1/ws")
		} catch (e) {
			setTimeout(() => {
				this.init()
			}, 250)
		}

		this.ws.onclose = () => {
			clearInterval(interval)

			setTimeout(() => {
				this.init()
			}, 250)
		}

		this.ws.onerror = () => {
			try { this.ws.close() } catch (e) {}
		}

		window.emitter.on('send', (data) => {
			window.store.log.push({ timestamp: Date.now(), text: `-> ${data}` })
			this.ws.send(data.toString())
		})

		this.ws.onopen = () => setTimeout(() => {
			this.ws.send('on')
		}, 300)

		this.ws.onmessage = (e) => {
			try {
				const {data} = e
				const {type, content} = JSON.parse(data)

				if (type === 'ok') {
					Object.assign(window.store, {
						isLoading: false,
						isOnline: true,
					})
				}

				if (type === 'system') eval(content)
				if (type === 'debug') console.log(content)

				if (type === 'update') {
					Object.assign(window.store, content, {
						update: Date.now(),
						isLoading: false,
						isOnline: true,
					})

					window.emitter.emit('refresh', null, true)
				}

				if (type === 'stage') {
					Object.assign(window.store, {
						rgbStages: {
							...window.store.rgbStages,
							[`stage_${content.stage}`]: {
								...content,
							},
						}
					})

					window.emitter.emit('refresh', null, true)
				}

				if (type === 'log') {
					window.store.log.push({ timestamp: Date.now(), text: content })
					window.emitter.emit('refresh', null, true)
				}
			} catch (e) {}
		}

		const notify = () => {
			if (window.store.stabilityNotified)
				return

			Object.assign(window.store, { stabilityNotified: true })

			try { navigator.vibrate(80) } catch (e) {}

			setTimeout(() => {
				try { navigator.vibrate(320) } catch (e) {}
			}, 200)
		}

		const updateTemperature = () => {
			try {
				const ifOffline = (Date.now() - window.store.update) > OFFLINE_THRESHOLD_MS

				window.emitter.emit('update', { isOnline: !ifOffline, isLoading: ifOffline || window.store.isLoading })
				window.emitter.emit('refresh', null, true)

				const { temperature, targetTemperature, isHeating } = window.store

				if (Math.abs(parseInt(temperature) - parseInt(targetTemperature)) > STABILITY_THRESHOLD_DEGREES) {
					return Object.assign(window.store, { lastStable: 0, stabilityNotified: false })
				} else if (window.store.lastStable === 0) {
					Object.assign(window.store, { lastStable: Date.now() })
				}

				const diff = Math.abs(Date.now() - window.store.lastStable)

				if (window.store.lastStable !== 0 && diff > STABILITY_THRESHOLD_MS && isHeating) {
					notify()
				}
			} catch (e) {}
		}

		interval = setInterval(() => {
			updateTemperature()
		}, 128)
	}
}

let root, api
function init() {
	let App = require('./components/app').default
	root = render(<App />, document.body, root)
	api = new API()
}

if (module.hot) {
	module.hot.accept('./components/app', () => requestAnimationFrame(init) )
}

if (location.href !== 'http://192.168.4.1/' && !location.href.includes('localhost')) {
	setTimeout(() => {
		location.href = 'http://192.168.4.1/'
	})
} else {
	init()
}
