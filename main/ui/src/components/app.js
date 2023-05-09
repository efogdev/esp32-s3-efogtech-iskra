import { h, Component } from 'preact';
import { Router } from 'preact-router';

import Header from './header';
import Home, {AnimationPicker, Slider} from './home';
import Profile from './profile';
import style from './style.less';
import cn from 'classnames';

window.store = {
	isServerThinking: false,
	update: Date.now(),
	isLoading: true,
	isHeating: false,
	temperature: 'N/A',
	voltage: 'N/A',
	isVoltageOk: false,
	isOnline: false,
	isCooling: false,
	coolingTemperature: 0,
	brightness: 0,
	speed: 0,
	isModalOpened: false,
	isStageEditorOpened: false,
	targetTemperature: 240,
	boardTemperature: 'N/A',
	heaterPower: 'N/A',
	coolerPower: 'N/A',
	fanPower: 'N/A',
	freeRam: 'N/A',
	voltageRaw: 'N/A',
	'12V': false,
	'15V': false,
	'20V': false,
	rgbStages: {},
	rgbStageEditing: null,
	rgbCurrentFn: 0,
	authEn: false,
	lastStable: Date.now(),
	stabilityNotified: false,
	log: [
		{ timestamp: Date.now(), text: 'UI launched' },
	],
}

export const rgbFnBinding = {
	'0': 'Off', // PWM_FN_OFF
	'1': 'Pulse', // PWM_FN_PULSE
	'2': 'Fade in', // PWM_FN_FADE_IN
	'3': 'Fade out', // PWM_FN_FADE_OUT
	'4': 'Fade', // PWM_FN_FADE
}

let updateCache = {};

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

	emit(event, data = null, nocache = true) {
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

			try {
				it.callback(data)
			} catch (e) {}

			if (it.once)
				toRemove.push(index)
		})

		this.handlers = this.handlers.filter((it, index) => !toRemove.includes(index))

		if (event === 'update') {
			if (!updateCache)
				updateCache = { ...window.store };

			const diff = Object.keys(data)
				.filter(key => updateCache[key] !== data[key] && key !== 'update')

			if (!diff.length)
				return

			window.store.log.push({
				timestamp: Date.now(),
				text: diff.map(key => `${key} = ${data[key]}`).join(', '),
			});

			updateCache = { ...window.store };
		}
	}
}

class StageEditor extends Component {
	constructor(props) {
		super(props)

		this.state = {
			values: [ ],
			_brightness: 100,
			_speed: 100,
			_fn: -1,
			_stageString: null,
			_stageIndex: -1,
		}

		this.refs = {}
	}

	componentDidMount() {
		window.emitter.on('refresh', () => {
			setTimeout(() => this.forceUpdate())
		})
	}

	static hexToRgbString(hexColor) {
		const shorthandRegex = /^#?([a-f\d])([a-f\d])([a-f\d])$/i
		const hex = hexColor.replace(shorthandRegex, (m, r, g, b) => r + r + g + g + b + b)
		const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex)

		const obj = result ? {
			r: parseInt(result[1], 16),
			g: parseInt(result[2], 16),
			b: parseInt(result[3], 16)
		} : { r: 0, g: 0, b: 0 }

		return `${obj.r},${obj.g},${obj.b}`
	}

	componentDidUpdate(previousProps, previousState, previousContext) {
		const { rgbStageEditing, rgbStages } = Object.assign({}, this.state, window.store)
		const { _brightness, _speed, _stageString, values, _fn } = this.state
		const stageString = StageEditor.makeStageString(_brightness, _speed, values, _fn)
		const storeStage = rgbStages[`stage_${rgbStageEditing}`];

		if (storeStage) {
			const _values = StageEditor.dataToValues(storeStage.data)
			const storeStageString = StageEditor.makeStageString(storeStage.power, storeStage.speed, _values, parseInt(storeStage.fn))

			if (storeStageString !== stageString && this.state._stageIndex !== rgbStageEditing) {
				this.setState({
					_stageString: storeStageString,
					_brightness: parseInt(storeStage.power),
					_speed: parseInt(storeStage.speed),
					_fn: parseInt(storeStage.fn),
					_stageIndex: rgbStageEditing,
					values: _values,
				})
			}
		}

		if (_stageString === stageString)
			return

		this.setState({ _stageString: stageString })
		this.props.onChange && this.props.onChange(stageString)
	}

	static dataToValues(dataString) {
		try {
			const parts = dataString.split(' ').filter(it => it.includes(':'))
			const values = []

			for (const part of parts) {
				const [, rgbString] = part.split(':')
				const [ r, g, b ] = rgbString.split(',').map(it => parseInt(it))

				values.push(`#${((1 << 24) + (r << 16) + (g << 8) + b).toString(16).substring(1)}`)
			}

			return values
		} catch (e) {
			return []
		}
	}

	static makeStageString(brightness, speed, values, fn) {
		const dataString = `${values.length} ${values.map((hexColor, i) => `${i}:${StageEditor.hexToRgbString(hexColor)}`).join(' ')}`

		return `fn=${parseInt(fn)} speed=${speed} power=${brightness} data=${dataString}`
	}

	setRef(ref, index) {
		this.refs[index] = ref
		this.updateColor(index)
	}

	updateColor(index) {
		if (!this.refs[index])
			return

		this.refs[index].querySelector('div').style.background = this.state.values[index]
	}

	click(index) {
		if (!this.refs[index])
			return

		this.refs[index].querySelector('input[type="color"]').click()
	}

	render() {
		const { values, _brightness, _speed, _fn } = this.state

		return (
			<div className={style.pickers}>
				<Slider alt name="Brightness" value={_brightness} onChange={value => this.setState({ _brightness: value })} />
				<Slider alt name="Speed" value={_speed} onChange={value => this.setState({ _speed: value })} />

				<AnimationPicker value={_fn} onChange={value => this.setState({ _fn: value })} />

				<div className={style.spacer} />

				{values.map((it, index) => (
					<div
						ref={ref => ref && this.setRef(ref, index)}
						key={index}
						className={style.picker}
						onClick={() => this.click(index)}
					>
						<div className={style.visual}></div>

						<input
							type="color"
							value={it}
							onChange={(e) => {
								if (values.length > 12)
									return alert('Sorry, currently the limit is 12 colors.')

								const newValues = this.state.values.slice()
								newValues[index] = e.target.value

								this.setState({ values: newValues })
							}}
						/>

						{it || '#000000'}

						<div onClick={e => {
							if (values.length <= 1)
								return

							e.stopPropagation()
							e.preventDefault()

							const newValues = this.state.values.slice()
							newValues.splice(index, 1)

							this.setState({ values: newValues })
						}} className={style.remove}>x</div>
					</div>
				))}

				<div className={style.controls}>
					<button onClick={() => this.setState({ values: [ ...this.state.values, '#673ab7' ] })}>Add color</button>
				</div>
			</div>
		);
	}
}

class Overlay extends Component {
	constructor(props) {
		super(props)

		this.state = {
			_stageData: null,
		}
	}

	componentDidMount() {
		window.addEventListener('beforeinstallprompt', (e) => {
			e.preventDefault();
			e.prompt();
		})

		window.emitter.on('refresh', () => {
			setTimeout(() => this.forceUpdate())
		})
	}

	heat() {
		const text = parseInt(document.querySelector('#modal-input').value)

		if (isNaN(text) || text < 160 || text > 240)
			return alert(`Sorry, but the limit is 160-240°C.`)

		window.emitter.emit('update', { targetTemperature: text, isModalOpened: false, isLoading: true, isHeating: true })

		window.emitter.emit('send', `set t=${text}`, true)
		window.emitter.emit('send', `heat`, true)

		try { navigator.vibrate(160) } catch (e) {}
	}

	saveStage(stageIndex) {
		window.emitter.emit('send', `save_stage stage=${stageIndex} ${this.state._stageData}`, true)
		window.emitter.emit('update', { isStageEditorOpened: false, rgbStageEditing: null, isLoading: true })

		try { navigator.vibrate(160) } catch (e) {}
	}

	render() {
		const { isModalOpened, isStageEditorOpened, rgbStageEditing } = Object.assign({}, this.state, window.store)

		return (
			<div className={cn(style.overlay, { 'flex': isModalOpened || isStageEditorOpened })}>
				<div className={cn(style.modal, { [style.hide]: !isModalOpened })}>
					<div className={style.title}>Target temperature:</div>
					<div>
						<input autocomplete="off" placeholder="160-240°C" type="number" id="modal-input" />
					</div>
					<div>
						<button onClick={() => this.heat()}  className={style.primary}>Heat</button>
						<button onClick={() => window.emitter.emit('update', { isModalOpened: false })} className={style.red}>Cancel</button>
					</div>
				</div>

				<div className={cn(style.modal, style.colors, { [style.hide]: !isStageEditorOpened })}>
					<div className={style.title}>Stage editor</div>

					<StageEditor onChange={value => this.setState({ _stageData: value })} />

					<div className={style.controls}>
						<button onClick={() => this.saveStage(rgbStageEditing)}  className={style.primary}>Set</button>
						<button onClick={() => window.emitter.emit('update', { isStageEditorOpened: false })} className={style.red}>Cancel</button>
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
