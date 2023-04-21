import { h, Component } from 'preact';
import style from './style.less';

export default class Settings extends Component {
	constructor(props) {
		super(props)

		this.state = {}
	}

	componentDidMount() {
		window.emitter.on('refresh', () => {
			setTimeout(() => this.forceUpdate())
		})
	}

	componentWillUnmount() {
	}

	render() {
		const reboot = () => {
			window.emitter.emit('update', { isLoading: true })
			window.emitter.emit('send', 'reboot')

			setTimeout(() => {
				location.reload()
			}, 1500)
		}

		return (
			<div className={style.firmware}>
				<div><button onClick={() => reboot()}>Reboot</button></div>
				<div><input id="btn" type="button" value="Firmware update" onClick={() => location.href = '/ota'} /></div>
			</div>
		);
	}
}
