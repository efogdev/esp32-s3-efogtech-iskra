import { h, Component } from 'preact';
import { Router } from 'preact-router';

import Header from './header';
import Home from './home';
import Profile from './profile';

window.store = {
	isLoading: true,
	isHeating: false,
	temperature: -1,
	voltage: '5V',
	isVoltageOk: true,
	isOnline: true,
}

window.emitter = new class EventEmitter {
	handlers = [];

	on(event, callback) {
		this.handlers.push({ event, callback })
	}

	off(event, callback) {
		this.handlers.splice(this.handlers.findIndex(it => it.callback === callback))
	}

	once(event, callback) {
		this.handlers.push({ event, callback, once: true })
	}

	emit(event, data = null) {
		const toRemove = []

		console.log('[EVENT]', event, data)

		this.handlers.forEach((it, index) => {
			if (!it || !it.callback)
				return

			if (it.event !== event)
				return

			it.callback(data)

			if (it.once)
				toRemove.push(index)
		})

		this.handlers = this.handlers.filter((it, index) => !toRemove.includes(index))
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
	}

	handleRoute = e => {
		this.currentUrl = e.url;
	}

	render() {
		return (
			<div id="app">
				<Header />
				<Router onChange={this.handleRoute}>
					<Home path="/" />
					<Profile path="/settings" />
				</Router>
			</div>
		);
	}
}
