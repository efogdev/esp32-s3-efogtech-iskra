import { h, Component } from 'preact';
import { Router } from 'preact-router';

import Header from './header';
import Home from './home';
import Profile from './profile';
import style from './style.less';
import cn from 'classnames';

window.store = {
	isLoading: true,
	isHeating: false,
	temperature: '?',
	voltage: '?',
	isVoltageOk: true,
	isOnline: true,
	isCooling: false,
	coolingTemperature: 4095,
	isModalOpened: false,
	targetTemperature: 240,
}

window.emitter = new class EventEmitter {
	handlers = [];
	cache = {};

	on(event, callback) {
		this.handlers.push({ event, callback })
	}

	off(event, callback) {
		this.handlers.splice(this.handlers.findIndex(it => it.callback === callback))
	}

	once(event, callback) {
		this.handlers.push({ event, callback, once: true })
	}

	emit(event, data = null, nocache = false) {
		const toRemove = []

		this.handlers.forEach((it, index) => {
			if (!it || !it.callback)
				return

			if (it.event !== event)
				return

			if (!nocache) {
				if (this.cache[event] && this.cache[event].callback === it.callback && Object.keys(data).every((key, index) => data[key] === this.cache[event][key]))
					return this

				if (!this.cache[event])
					this.cache[event] = []

				if (this.cache[event].some(cacheItem => Object.keys(data).every(key => data[key] === cacheItem[key]) && cacheItem.callback === it.callback))
					return

				this.cache[event].push({ ...data, callback: it.callback })
			}

			console.log(`[EVENT]`, event, data)

			try {
				it.callback(data)
			} catch (e) {}

			console.log(`[STORE]`, window.store)

			if (it.once)
				toRemove.push(index)
		})

		this.handlers = this.handlers.filter((it, index) => !toRemove.includes(index))
	}
}

class Overlay extends Component {
	constructor(props) {
		super(props)

		this.state = {}
	}

	componentDidMount() {
		window.emitter.on('refresh', () => {
			setTimeout(() => this.forceUpdate())
		})
	}

	heat() {
		const text = parseInt(document.querySelector('#modal-input').value)

		if (isNaN(text) || text < 120 || text > 320)
			return alert(`Sorry, but the limit is 120-320°C.`)

		window.emitter.emit('update', { targetTemperature: text, isModalOpened: false, isLoading: true, isHeating: true });

		window.emitter.emit('send', `set t=${text}`, true)
		window.emitter.emit('send', `heat`, true)
	}

	render() {
		const { isModalOpened } = Object.assign({}, this.state, window.store)

		return (
			<div className={cn(style.overlay, { 'flex': isModalOpened })}>
				<div className={style.modal}>
					<div className={style.title}>Target temperature:</div>
					<div>
						{/* :D */}
						<input placeholder="120-320°C" type="number" id="modal-input" />
					</div>
					<div>
						<button onClick={this.heat}  className={style.primary}>Heat</button>
						<button onClick={() => window.emitter.emit('update', { isModalOpened: false })} className={style.red}>Cancel</button>
					</div>
				</div>
			</div>
		)
	}
}

export default class App extends Component {
	getChildContext() {
		return window.store
	}

	componentDidMount() {
		try {
			if (!localStorage.getItem('store')) {
				localStorage.setItem('store', JSON.stringify(window.store))
			} else {
				Object.assign(window.store, JSON.parse(localStorage.getItem('store')))
			}
		} catch (e) {}

		window.emitter.on('update', data => {
			const storeString = JSON.stringify(Object.values(window.store).sort())
			Object.assign(window.store, data)

			if (JSON.stringify(Object.values(window.store).sort()) !== storeString) {
				window.emitter.emit('refresh', null, true)
			}
		})
	}

	handleRoute = e => {
		this.currentUrl = e.url;
	}

	render() {
		return (
			<div id="app">
				<Overlay />
				<Header />
				<Router onChange={this.handleRoute}>
					<Home path="/" />
					<Profile path="/settings" />
				</Router>
			</div>
		);
	}
}
