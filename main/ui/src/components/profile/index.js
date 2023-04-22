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

		const pdTest = () => {
			window.emitter.emit('update', { isLoading: true })
			window.emitter.emit('send', `pd_test`)

			setTimeout(() => {
				this.setState({ tried: true })
			}, 1000)
		}

		return (
			<div className={style.firmware}>
				<div className={style.card}>
					<div className={style.version}>
						Firmware: {window.__firmwareVersion}
					</div>

					<div className={style.inline}><button onClick={() => pdTest()}>Test PSU</button></div>
					<div className={cn(style.inline, { [style.capable]: data['12V'], [style.red]: data.tried && !data['12V'] })}><button onClick={() => pd(12)}>12V</button></div>
					<div className={cn(style.inline, { [style.capable]: data['15V'], [style.red]: data.tried && !data['15V']  })}><button onClick={() => pd(15)}>15V</button></div>
					<div className={cn(style.inline, { [style.capable]: data['20V'], [style.red]: data.tried && !data['20V']  })}><button onClick={() => pd(20)}>20V</button></div>

					<br />

					<div className={style.inline}><button onClick={() => reboot()}>Reboot</button></div>
					<div className={style.inline}><input id="btn" type="button" value="Firmware update" onClick={() => this.setState({ showOta: !data.showOta })} /></div>
				</div>

				<div className={style.card}>
					<iframe className={cn(style.frame, { [style.visible]: data.showOta })} src="/ota" />
				</div>
			</div>
		);
	}
}
