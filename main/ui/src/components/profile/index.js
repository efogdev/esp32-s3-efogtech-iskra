import { h, Component } from 'preact';
import cn from 'classnames';
import style from './style.less';

export default class Settings extends Component {
	constructor(props) {
		super(props)

		this.state = {
			tried: false,
			showOta: false,
		}
	}

	componentDidMount() {
		window.emitter.on('refresh', () => {
			setTimeout(() => this.forceUpdate())
		})
	}
	render() {
		const data = Object.assign({}, this.state, window.store)

		const reboot = () => {
			window.emitter.emit('update', { isLoading: true })
			window.emitter.emit('send', 'reboot')

			setTimeout(() => {
				location.reload()
			}, 1500)
		}

		const pd = (volts) => {
			window.emitter.emit('update', { isLoading: true })
			window.emitter.emit('send', `pd_request ${volts}`)
			location.href = '/';
		}

		const stage = (stageIndex) => {
			window.emitter.emit('update', { isStageEditorOpened: true, rgbStageEditing: stageIndex })
		}

		const pdTest = () => {
			window.emitter.emit('update', { isLoading: true })
			window.emitter.emit('send', `pd_test`)

			setTimeout(() => {
				this.setState({ tried: true })
			}, 1000)
		}

		const toggleAuth = () => {
			window.emitter.emit('send', `auth`)
			window.emitter.emit('update', { isLoading: true })
		}

		return (
			<div className={style.firmware}>
				<div className={style.card}>
					<div className={style.version}>
						Firmware: {window.__firmwareVersion}
					</div>

					RGB stages:<br />
					<div className={cn(style.inline)}><button onClick={() => stage(0)}>Waiting</button></div>
					<div className={cn(style.inline)}><button onClick={() => stage(1)}>Idle</button></div>
					<div className={cn(style.inline)}><button onClick={() => stage(2)}>Heating</button></div>

					<br />
					<br />

					<div className={cn(style.inline, { [style.capable]: data['12V'], [style.red]: !data['12V'] })}><button disabled={!data['12V']} onClick={() => pd(12)}>20W</button></div>
					<div className={cn(style.inline, { [style.capable]: data['15V'], [style.red]: !data['15V']  })}><button disabled={!data['15V']} onClick={() => pd(15)}>30W</button></div>
					<div className={cn(style.inline, { [style.capable]: data['20V'], [style.red]: !data['20V']  })}><button disabled={!data['20V']} onClick={() => pd(20)}>65W</button></div>

					<br />
					<br />

					<div className={style.inline}><button onClick={() => location.reload()}>Refresh</button></div>
					<div className={style.inline}><button onClick={() => reboot()}>Reboot</button></div>

					<br />

					<div className={style.inline}><input id="btn" type="button" value="Firmware" onClick={() => this.setState({ showOta: !data.showOta })} /></div>
					<div className={style.inline}><button onClick={() => toggleAuth()}>DNS: {data.authEn ? 'yes' : 'no'}</button></div>

					<iframe sandbox="allow-same-origin allow-scripts allow-popups allow-forms" className={cn(style.frame, { [style.visible]: data.showOta })} src="/ota" />
				</div>

				<div className={style.card}>
					<div className={style.log}>
						{data.log.map((item, index) => (
							<div key={index} className={style.item}>
								<div className={style.time}>{new Date(item.timestamp).toLocaleTimeString()}</div>
								<div className={style.text}>{item.text}</div>
							</div>
						))}
					</div>
				</div>
			</div>
		);
	}
}
