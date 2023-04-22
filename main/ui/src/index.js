import { h, render } from 'preact';
import './style';

const OFFLINE_THRESHOLD_MS = 600;
let interval = -1;

class API {
	ws = null;

	constructor() {
		this.tryInit()
	}

	initSW() {
		try {
			navigator.serviceWorker.register('/sw.js')

			console.log(navigator.serviceWorker);

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
			console.log(e);

			setTimeout(() => {
				this.tryInit()
			}, 5000)
		}
	}

	init() {
		this.ws = new WebSocket("ws://192.168.4.1/ws");

		this.ws.onclose = this.init.bind(null);
		this.ws.onerror = this.init.bind(null);

		window.emitter.on('send', (data) => {
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

					window.emitter.emit('send', `pd_request 15`)
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
			} catch (e) { alert(e) }
		};

		const updateTemperature = () => {
			const ifOffline = (Date.now() - window.store.update) > OFFLINE_THRESHOLD_MS;

			Object.assign(window.store, {
				update: Date.now(),
			});

			window.emitter.emit('update', { update: Date.now(), isOnline: !ifOffline, isLoading: ifOffline || window.store.isLoading })
		};

		setInterval(() => {
			updateTemperature()
		}, 360)
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

init();
