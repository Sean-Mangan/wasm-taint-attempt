# wasm-taint-attempt

### Steps to run:

#### 1. Clone the repository for node js 
	"git clone git@github.com:nodejs/node.git"

#### 2. Navigate to v8 src file
	"cd node/deps/v8"
	
#### 3. Checkout Node v10
	"git checkout tags/v10.0.0"
	
#### 4. Delete the src file
	"rm -rf src"
	
#### 5. Clone this repository in its place
	"git clone git@github.com:Sean-Mangan/wasm-taint-attempt.git"
	
#### 6. Navigate to main node folder
	"cd ../.."
	
#### 7. Configure node
	"sudo ./configure"
	
#### 8. Build Node
	"sudo make -j4"
