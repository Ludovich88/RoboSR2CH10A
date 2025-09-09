console.log('Loading sr2ch10a.js');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const e = exposes.presets;
const ea = exposes.access;

// Глобальный кэш состояний для отслеживания изменений
const previousStates = {};

const definition = {
    zigbeeModel: ['SR2CH10A'],
    model: 'SR2CH10A',
    vendor: 'Robo',
    description: 'RoboSR2CH10A',
    fromZigbee: [fz.on_off, fz.identify, {
        cluster: 'genOnOff',
        type: ['attributeReport', 'readResponse', 'commandOn', 'commandOff', 'commandToggle'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.hasOwnProperty('onOff')) {
                const endpoint = msg.endpoint.ID;
                const currentState = msg.data.onOff;
                const previousState = previousStates[endpoint];
                
                console.log(`On/Off received from endpoint ${endpoint}: ${currentState ? 'ON' : 'OFF'} (previous: ${previousState !== undefined ? (previousState ? 'ON' : 'OFF') : 'undefined'})`);
                
                // Публикуем action только если состояние изменилось
                if (previousState !== currentState) {
                    console.log(`State changed for endpoint ${endpoint}: ${previousState} → ${currentState}`);
                    
                    if (endpoint === 1) {
                        publish({
                            state_1: currentState ? 'ON' : 'OFF',
                            action: currentState ? 'on_l1' : 'off_l1'
                        });
                    } else if (endpoint === 2) {
                        publish({
                            state_2: currentState ? 'ON' : 'OFF',
                            action: currentState ? 'on_l2' : 'off_l2'
                        });
                    }
                } else {
                    console.log(`No state change for endpoint ${endpoint}, updating state only`);
                    
                    // Только обновляем состояние без action
                    if (endpoint === 1) {
                        publish({state_1: currentState ? 'ON' : 'OFF'});
                    } else if (endpoint === 2) {
                        publish({state_2: currentState ? 'ON' : 'OFF'});
                    }
                }
                
                // Сохраняем текущее состояние
                previousStates[endpoint] = currentState;
                
                return {};
            }
            
            // Обработка команд On/Off
            if (msg.type === 'commandOn' || msg.type === 'commandOff' || msg.type === 'commandToggle') {
                const endpoint = msg.endpoint.ID;
                let state;
                
                if (msg.type === 'commandOn') {
                    state = true;
                } else if (msg.type === 'commandOff') {
                    state = false;
                } else if (msg.type === 'commandToggle') {
                    // Для toggle нужно получить текущее состояние
                    state = !msg.data.onOff;
                }
                
                console.log(`On/Off command received from endpoint ${endpoint}: ${state ? 'ON' : 'OFF'}`);
                
                // Команды всегда генерируют action
                if (endpoint === 1) {
                    publish({
                        state_1: state ? 'ON' : 'OFF',
                        action: state ? 'on_l1' : 'off_l1'
                    });
                } else if (endpoint === 2) {
                    publish({
                        state_2: state ? 'ON' : 'OFF',
                        action: state ? 'on_l2' : 'off_l2'
                    });
                }
                
                // Обновляем кэш для команд
                previousStates[endpoint] = state;
                
                return {};
            }
        },
    }],
    toZigbee: [tz.on_off, tz.identify, {
        key: ['state_1', 'state_2', 'state'],
        convertSet: async (entity, key, value, meta) => {
            let endpoint, state;
            
            if (key === 'state_1') {
                endpoint = 1;
                state = value === 'ON';
            } else if (key === 'state_2') {
                endpoint = 2;
                state = value === 'ON';
            } else if (key === 'state') {
                // Обработка команды 'state' - определяем endpoint по meta
                endpoint = meta.endpoint || 1;
                state = value === 'ON';
            } else {
                return;
            }
            
            logger.info(`Setting endpoint ${endpoint} to ${value}`);
            
            const ep = entity.getEndpoint(endpoint);
            if (ep) {
                await ep.write('genOnOff', {onOff: state}, {manufacturerCode: 0xA0FF});
            }
            
            // Возвращаем правильное поле
            const stateKey = endpoint === 1 ? 'state_1' : 'state_2';
            return {state: {[stateKey]: value}};
        },
        convertGet: async (entity, key, meta) => {
            let endpoint;
            
            if (key === 'state_1') {
                endpoint = 1;
            } else if (key === 'state_2') {
                endpoint = 2;
            } else if (key === 'state') {
                endpoint = meta.endpoint || 1;
            } else {
                return;
            }
            
            logger.info(`Getting state for endpoint ${endpoint}`);
            
            const ep = entity.getEndpoint(endpoint);
            if (ep) {
                const result = await ep.read('genOnOff', ['onOff'], {manufacturerCode: 0xA0FF});
                const stateKey = endpoint === 1 ? 'state_1' : 'state_2';
                return {state: {[stateKey]: result.onOff ? 'ON' : 'OFF'}};
            }
            return {state: {[endpoint === 1 ? 'state_1' : 'state_2']: 'OFF'}};
        },
    }],
    exposes: [
        e.switch().withEndpoint('1').withDescription('Relay 1 control (toggle via short button press)'),
        e.switch().withEndpoint('2').withDescription('Relay 2 control'),
        // e.identify().withDescription('Identify the device (via Zigbee or long button press for pairing)'), // TODO: Пока не реализовано в коде устройства - нужно добавить обработку команд identify в main.c
    ],
    meta: {
        multiEndpoint: true,
        manufacturerCode: 0xA0FF,
    },
    endpoint: (device) => ({
        1: 1,
        2: 2,
    }),
    onEvent: async (type, data, device, options, state, logger) => {
        if (type === 'deviceJoined') {
            console.log('RoboSR2CH10A connected to the network as a router');
        } else if (type === 'deviceLeave') {
            console.log('RoboSR2CH10A left the network');
        } else if (type === 'deviceInterview') {
            console.log('RoboSR2CH10A completed the interview');
            
            // Периодически читаем состояние для синхронизации
            setInterval(async () => {
                try {
                    const ep1 = device.getEndpoint(1);
                    const ep2 = device.getEndpoint(2);
                    
                    if (ep1) {
                        await ep1.read('genOnOff', ['onOff'], {manufacturerCode: 0xA0FF});
                    }
                    if (ep2) {
                        await ep2.read('genOnOff', ['onOff'], {manufacturerCode: 0xA0FF});
                    }
                    console.log('Periodic state sync completed');
                } catch (error) {
                    console.warn('Periodic state sync failed:', error.message);
                }
            }, 30000); // Каждые 30 секунд
        }
    },
    // Кастомная функция для правильного маппинга полей
    onZigbeeMessage: (type, data, device, options, state, logger) => {
        if (type === 'attributeReport' && data.cluster === 'genOnOff' && data.data.hasOwnProperty('onOff')) {
            const endpoint = data.endpoint.ID;
            const state = data.data.onOff;
            console.log(`On/Off received from endpoint ${endpoint}: ${state ? 'ON' : 'OFF'}`);
            
            // Публикуем правильные поля
            if (endpoint === 1) {
                device.publish({state_1: state ? 'ON' : 'OFF'});
            } else if (endpoint === 2) {
                device.publish({state_2: state ? 'ON' : 'OFF'});
            }
        }
    },
    configure: async (device, coordinatorEndpoint, logger) => {
        try {
            logger.info('Configuring RoboSR2CH10A Zigbee Router...');
            const ep1 = device.getEndpoint(1);
            if (ep1) {
                await reporting.bind(ep1, coordinatorEndpoint, ['genOnOff', 'genBasic', 'genIdentify']);
                await reporting.onOff(ep1, { min: 0, max: 300, change: 1 }); // Отправка изменений при изменении на 1
                logger.info('Relay 1 (Endpoint 1) configured with immediate on/off reporting');
            }
            const ep2 = device.getEndpoint(2);
            if (ep2) {
                await reporting.bind(ep2, coordinatorEndpoint, ['genOnOff', 'genBasic', 'genIdentify']);
                await reporting.onOff(ep2, { min: 0, max: 300, change: 1 }); // Отправка изменений при изменении на 1
                logger.info('Relay 2 (Endpoint 2) configured with immediate on/off reporting');
            }
            for (let ep of [ep1, ep2]) {
                if (ep) {
                    await reporting.basic(ep, { min: 3600, max: 7200, change: [] });
                    // Читаем атрибуты
                    try {
                        await ep.read('genBasic', ['powerSource']);
                        console.log(`Power source attribute read for endpoint ${ep.endpointID}`);
                    } catch (error) {
                        console.warn(`Failed to read power source for endpoint ${ep.endpointID}: ${error.message}`);
                    }
                    // Читаем текущее состояние On/Off
                    try {
                        await ep.read('genOnOff', ['onOff']);
                        console.log(`On/Off state read for endpoint ${ep.endpointID}`);
                        
                        // Принудительно запрашиваем отчет об атрибуте
                        await ep.read('genOnOff', ['onOff'], {manufacturerCode: 0xA0FF});
                        console.log(`On/Off attribute report requested for endpoint ${ep.endpointID}`);
                    } catch (error) {
                        console.warn(`Failed to read On/Off state for endpoint ${ep.endpointID}: ${error.message}`);
                    }
                }
            }
            logger.info('RoboSR2CH10A successfully configured');
        } catch (error) {
            console.error('Error configuring RoboSR2CH10A:', error);  // Используем console.error вместо logger.error
        }
    },
};

module.exports = definition;