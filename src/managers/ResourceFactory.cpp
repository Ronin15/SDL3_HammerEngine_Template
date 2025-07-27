void ResourceFactory::clear() {
  auto &creators = getCreators();
  creators.clear();
  RESOURCE_DEBUG("ResourceFactory::clear - Cleared all resource creators");
}
