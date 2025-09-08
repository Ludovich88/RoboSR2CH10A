const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const e = exposes.presets;
const ea = exposes.access;

const definition = {
    // Поддержка модели: RoboSR2CH10A
    zigbeeModel: ['RoboSR2CH10A'],
    model: 'RoboSR2CH10A',
    vendor: 'Robonomics',
    description: 'Zigbee Router with 2 Channel Relay Control',
    fromZigbee: [fz.on_off],
    toZigbee: [tz.on_off],
    exposes: [
        // Реле 1 (Endpoint 1)
        e.switch()
            .withEndpoint('l1')
            .withAccess('state', ea.STATE_GET)
            .withDescription('Relay 1 control'),
        
        // Реле 2 (Endpoint 2)  
        e.switch()
            .withEndpoint('l2')
            .withAccess('state', ea.STATE_GET)
            .withDescription('Relay 2 control'),
    ],
    meta: {
        multiEndpoint: true,
        configureKey: 1,
        // Поддержка manufacturer-specific атрибутов
        manufacturerCode: 0xA0FF,
    },
    endpoint: (device) => {
        return {
            l1: 1,        // Реле 1
            l2: 2,        // Реле 2
        };
    },
    configure: async (device, coordinatorEndpoint, logger) => {
        try {
            logger.info('Configuring RoboSR2CH10A device...');
            
            // Настройка отчетов для реле (Endpoints 1 и 2)
            if (device.getEndpoint(1)) {
                await reporting.bind(device.getEndpoint(1), coordinatorEndpoint, ['genOnOff']);
                await reporting.onOff(device.getEndpoint(1));
                logger.info('Relay 1 (Endpoint 1) configured');
            }
            
            if (device.getEndpoint(2)) {
                await reporting.bind(device.getEndpoint(2), coordinatorEndpoint, ['genOnOff']);
                await reporting.onOff(device.getEndpoint(2));
                logger.info('Relay 2 (Endpoint 2) configured');
            }
        
            logger.info('RoboSR2CH10A device configured successfully');
        } catch (error) {
            logger.error('Error configuring RoboSR2CH10A device:', error);
            throw error;
        }
    },
};

module.exports = definition;