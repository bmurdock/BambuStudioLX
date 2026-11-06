let nextDeviceSequence = 0;

export function allocateDeviceSequence(): number {
    return nextDeviceSequence++;
}
