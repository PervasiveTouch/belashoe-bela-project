const numSensors = 8;

var canvas, gridSize, padding;
var sensorCalibration;
var valueMapping; // Slider for amplification

function setup() {
    canvas = createCanvas(windowWidth, windowHeight);
    gridSize = min(width, height) / 4; // Grid cell size (4x4 grid)
    padding = gridSize * 0.1; // Padding around each cell
    
    sensorCalibration = [0.04, 0.04, 0.05, 0.06, 0.07, 0.06, 0.05, 0.06];
    
    // Create slider (1 to 10)
    valueMapping = createSlider(1, 15, 5, 0.5); // Default value is 5
    valueMapping.position(width-170, 20); // Position the slider
    valueMapping.style('width', '150px'); // Set slider width
}

function draw() {
    background(255); // Clear canvas
    noStroke();
    
    var intensity = valueMapping.value(); // Get amplification factor

    // Read touch data from Bela
    var touchData = Bela.data.buffers[0]; // Data sent on channel 0
    if (!touchData || touchData.length < numSensors) return; // Skip if no data or insufficient channels
    
    
    sensorCalibration = Bela.data.buffers[1];	
    console.log(sensorCalibration);
    
    // Normalization
    var normData = [];
    for (var i = 0; i < numSensors; i++) {
		var temp = touchData[i];
		normData.push(temp / sensorCalibration[i]);
    }
    
    // Loop through the individual lanes and calculate all possible grid points
    var gridData = [];
    for (var i = 7; i >= 4; i--) {
    	for (var j = 3; j >= 0; j--) {
    		gridData.push((normData[i]*normData[j]));
    	}
    }

    // Loop through 4x4 grid
    for (var row = 0; row < 4; row++) {
        for (var col = 0; col < 4; col++) {
        	if (!(row === 0 && col == 3)) {
	    		var index = row * 4 + col; // Map grid to linear index
	            if (index < gridData.length) {
	                var value = gridData[index]*5; // Get touch value
	                var cellX = col * gridSize; // X position
	                var cellY = row * gridSize; // Y position
	
	                // Map touch value to color intensity
	                var intensity1 = map(value, 0, 1, 0, 170, true); // Scale 0-1 to 0-255
	                var intensity2 = map(value, 1, intensity, 50, 170, true); // Scale 0-1 to 0-255
	                
	                fill(0, intensity1, intensity2);
	                rect(cellX + padding, cellY + padding, gridSize - 2 * padding, gridSize - 2 * padding);
	
	                // Add value as text inside the cell for debugging
	                fill(255);
	                textAlign(CENTER, CENTER);
	                text(value.toFixed(2), cellX + gridSize / 2, cellY + gridSize / 2);
	            }
        	}
        }
    }
    
    // Reset the arrays
    gridData = [];
    normData = [];
}
