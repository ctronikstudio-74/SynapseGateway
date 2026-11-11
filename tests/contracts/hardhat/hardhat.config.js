require("@nomicfoundation/hardhat-toolbox");

module.exports = {
  solidity: "0.8.20",
  paths: {
    sources: "../mocks",
    tests: "./test",
    cache: "./cache",
    artifacts: "./artifacts"
  }
};
