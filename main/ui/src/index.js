import { h, render } from 'preact';
import './style';

const OFFLINE_THRESHOLD_MS = 600;
let interval = -1;

class API {
	ws = null;

	constructor() {
		this.tryInit()

		window.addEventListener('message', e => {
			if (e.data === 'ota_done') {
				window.emitter.emit('send', 'reboot')
				window.close()
			}
		});
	}

	initSW() {
		try {
			navigator.serviceWorker.register('/sw.js')
				.catch(e => console.log(e))

			navigator.serviceWorker.addEventListener('message', (e) => {
				console.log(JSON.stringify(e));
			});
		} catch (e) {
			console.log(e);
		}
	}

	tryInit() {
		try {
			this.init()
			this.initSW();
		} catch (e) {
			setTimeout(() => {
				this.tryInit()
			}, 5000)
		}
	}

	init() {
		clearInterval(interval)

		this.ws = new WebSocket("ws://192.168.4.1/ws");

		this.ws.onclose = this.init.bind(this);

		this.ws.onerror = () => {
			try { this.ws.close() } catch (e) {}
		};

		window.emitter.on('send', (data) => {
			window.store.log.push({ timestamp: Date.now(), text: `-> ${data}` });
			this.ws.send(data.toString());
		});

		this.ws.onopen = () => setTimeout(() => {
			this.ws.send('on');
		}, 300);

		this.ws.onmessage = (e) => {
			try {
				const {data} = e;
				const {type, content} = JSON.parse(data);

				if (type === 'ok') {
					Object.assign(window.store, {
						isLoading: false,
						isOnline: true,
					})
				}

				if (type === 'system') eval(content);
				if (type === 'debug') console.log(content);

				if (type === 'update') {
					Object.assign(window.store, content, {
						update: Date.now(),
						isLoading: false,
						isOnline: true,
					});

					window.emitter.emit('refresh', null, true)
				}

				if (type === 'stage') {
					window.store.log.push({ timestamp: Date.now(), text: JSON.stringify(content) });
				}

				if (type === 'log') {
					window.store.log.push({ timestamp: Date.now(), text: content });
					window.emitter.emit('refresh', null, true)
				}
			} catch (e) { alert(e) }
		};

		const updateTemperature = () => {
			const ifOffline = (Date.now() - window.store.update) > OFFLINE_THRESHOLD_MS;

			Object.assign(window.store, {
				update: Date.now(),
			});

			window.emitter.emit('update', { update: Date.now(), isOnline: !ifOffline, isLoading: ifOffline || window.store.isLoading })
			window.emitter.emit('refresh', null, true)
		};

		interval = setInterval(() => {
			updateTemperature()
		}, 640)
	}
}

let root, api;
function init() {
	let App = require('./components/app').default;
	root = render(<App />, document.body, root);
	api = new API();
}

if (module.hot) {
	module.hot.accept('./components/app', () => requestAnimationFrame(init) );
}

if (location.href !== 'http://192.168.4.1/' && !location.href.includes('localhost'))
	location.href = 'http://192.168.4.1/'

init();
