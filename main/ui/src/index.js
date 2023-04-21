import { h, render } from 'preact';
import './style';

const OFFLINE_THRESHOLD_MS = 3000;
let interval = -1;

class API {
	ws = null;

	constructor() {
		this.tryInit()
	}

	tryInit() {
		try {
			this.init()
		} catch (e) {
			setTimeout(() => {
				this.tryInit()
			}, 5000)
		}
	}

	init() {
		clearInterval(interval);

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
			window.emitter.emit('update', { isOffline: (window.store.update || Date.now()) - Date.now() > OFFLINE_THRESHOLD_MS })
		};

		interval = setInterval(() => {
			updateTemperature()
		}, 1500)
	}
}

let root, api;
function init() {
	let App = require('./components/app').default;
	root = render(<App />, document.body, root);
	api = new API();
}

if (process.env.NODE_ENV === 'production') {
	require('./pwa');
}

if (module.hot) {
	module.hot.accept('./components/app', () => requestAnimationFrame(init) );
}

init();
