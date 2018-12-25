
<transform {
	network: <Network name>
	inputType: <Caffe|Protobuf|Gzip>
	inputDataType: <Float32|Fixed16>
	outputType: <Protobuf|Gzip>
	outputDataType: <Float32|Fixed16>
	[activate_bias_and_out_act: true]
}>

<simulate {
	network: <Network name>
	inputType: <Caffe|Protobuf|Gzip>
	inputDataType: <Float32|Fixed16>
	[activate_bias_and_out_act: true]
	<experiment {
		architecture: <BitPragmatic|Laconic>
		task: <Cycles|MemAccesses|Potentials>
	}>
}>